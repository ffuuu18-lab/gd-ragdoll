// dllmain.cpp - Grim Dawn "always ragdoll" mod.
//
// Loaded as ragdoll.asi by Ultimate ASI Loader (x64\dinput8.dll), alongside any other .asi.
// Exports nothing; only DllMain runs.
//
// ---------------------------------------------------------------------------------------------
// Where the decision lives
// ---------------------------------------------------------------------------------------------
// Character::ShouldDoRagDoll and GameEngine::AllowRagdolls are exported and look like the obvious
// hook points. They are not: a measurement run over 198 deaths recorded ZERO calls to either. The
// compiler inlined both into DefaultDeathHandler::Execute(bool) and left the out-of-line copies
// unreferenced. The branch the engine really runs, inlined at Execute + ~0x268:
//
//     chr->ragdollPhysics                      cmp byte [rdi+0x286C], 0    je  -> animation
//  && Entity::InRenderPreLoadFrustum(chr)      call                        je  -> animation
//  && Actor::HasRigidBodyData(chr)             call                        je  -> animation
//  && chr->doLateCrumple == 0                  cmp byte [rcx+0x286D], 0    jne -> animation
//  && gGameEngine->activeRagdolls < 5          cmp dword [rax+0x37690], 5  jge -> animation
//
// The ceiling of five is what makes corpses in a pack play canned animations.
//
// ---------------------------------------------------------------------------------------------
// Why this does NOT patch the code
// ---------------------------------------------------------------------------------------------
// An earlier version lifted the ceiling by rewriting that `jge` to two NOPs. It worked, but
// editing bytes inside a shared game DLL is a bad neighbour: other plugins locate their own
// targets by scanning the same loaded image for byte patterns, and an edit in the middle of a
// function makes those scans miss. The anchor used here (`48 8B 05 <gGameEngine> / 83 B8 <off>
// 05 / 7D`) is exactly the kind of distinctive sequence something else would anchor on too.
//
// So this leaves Game.dll byte-identical. The inlined test READS gGameEngine->activeRagdolls, so
// the hook presents a value that passes for the duration of the original call, then restores the
// true count plus whatever the engine added meanwhile. Same effect, nothing to collide with.
//
// The pattern scan is still here, but it is read-only: it exists to DECODE the address of the
// gGameEngine global and the offset of activeRagdolls straight out of the instructions, so that
// neither is hard-coded to a build.

#include <windows.h>
#include <MinHook.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gdr.h"
#include "gdr_version.h"

namespace {

// =============================================================================================
// config
// =============================================================================================
struct Config {
    bool enabled           = true;
    bool liftCap           = true;   // lift the ceiling at runtime, touching no game code
    int  maxActiveRagdolls = 0;      // 0 = no ceiling; N = allow while fewer than N are live
    bool patchBytes        = false;  // legacy: edit the jge in place. Breaks pattern scanners.
    bool ignoreRagdollFlag = false;  // ignore a DBR ragdollPhysics=0 opt-out (needs patchBytes)
    int  logLevel          = 1;      // 0 off, 1 init+counters, 2 one line per death
    int  summaryEvery      = 25;
    std::string logPath;
};

Config g_cfg;
CRITICAL_SECTION g_logLock;
bool g_logLockReady = false;

std::string ModuleDir(HMODULE m)
{
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(m, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::string();
    std::string p(buf, n);
    size_t slash = p.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string() : p.substr(0, slash + 1);
}

void LogF(int level, const char* fmt, ...)
{
    if (g_cfg.logLevel < level || g_cfg.logPath.empty() || !g_logLockReady) return;
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[32];
    _snprintf_s(stamp, sizeof(stamp), _TRUNCATE, "%02u:%02u:%02u.%03u ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    EnterCriticalSection(&g_logLock);
    FILE* fh = nullptr;
    if (fopen_s(&fh, g_cfg.logPath.c_str(), "a") == 0 && fh) {
        fputs(stamp, fh); fputs(line, fh); fputc('\n', fh);
        fclose(fh);
    }
    LeaveCriticalSection(&g_logLock);
}

void LoadConfig(const std::string& dir)
{
    g_cfg.logPath = dir + "ragdoll.log";
    const std::string ini = dir + "ragdoll.ini";
    FILE* fh = nullptr;
    if (fopen_s(&fh, ini.c_str(), "r") != 0 || !fh) return;

    char line[512];
    while (fgets(line, sizeof(line), fh)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == ';' || *p == '\r' || *p == '\n' || *p == '\0') continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = p;
        char* val = eq + 1;
        for (char* e = key + strlen(key); e > key && (e[-1] == ' ' || e[-1] == '\t'); --e) e[-1] = '\0';
        while (*val == ' ' || *val == '\t') ++val;
        for (char* e = val + strlen(val);
             e > val && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'); --e) {
            e[-1] = '\0';
        }
        if      (_stricmp(key, "enabled") == 0)           g_cfg.enabled = atoi(val) != 0;
        else if (_stricmp(key, "liftCap") == 0)           g_cfg.liftCap = atoi(val) != 0;
        else if (_stricmp(key, "maxActiveRagdolls") == 0) g_cfg.maxActiveRagdolls = atoi(val);
        else if (_stricmp(key, "patchBytes") == 0)        g_cfg.patchBytes = atoi(val) != 0;
        else if (_stricmp(key, "ignoreRagdollFlag") == 0) g_cfg.ignoreRagdollFlag = atoi(val) != 0;
        else if (_stricmp(key, "logLevel") == 0)          g_cfg.logLevel = atoi(val);
        else if (_stricmp(key, "summaryEvery") == 0)      g_cfg.summaryEvery = atoi(val);
        else if (_stricmp(key, "logPath") == 0 && *val)   g_cfg.logPath = val;
    }
    fclose(fh);
}

// =============================================================================================
// engine entry points + decoded sites
// =============================================================================================
Gdr_HasRigidBodyData_t       p_HasRigidBodyData = nullptr;
Gdr_InRenderPreLoadFrustum_t p_InFrustum        = nullptr;
Gdr_Execute_t                o_Execute          = nullptr;

uint8_t** g_gGameEngine = nullptr;   // decoded: address of the GameEngine* global
int       g_activeOff   = 0;         // decoded: offset of activeRagdolls within GameEngine
int       g_stockCap    = 0;         // the imm8 the game shipped with (5)
bool      g_bytesPatched = false;

volatile LONG c_exec = 0, c_willRagdoll = 0;
volatile LONG c_noRig = 0, c_optedOut = 0, c_offScreen = 0, c_lateCrumple = 0, c_overCap = 0;

int* ActiveRagdollsPtr()
{
    if (!g_gGameEngine || !g_activeOff) return nullptr;
    uint8_t* ge = *g_gGameEngine;
    if (!ge) return nullptr;
    return reinterpret_cast<int*>(ge + g_activeOff);
}

int ActiveRagdolls()
{
    const int* p = ActiveRagdollsPtr();
    return p ? *p : -1;
}

const char* CapLabel()
{
    if (!g_cfg.enabled || !g_cfg.liftCap) return "STOCK";
    return (g_cfg.maxActiveRagdolls >= 1) ? "custom" : "none";
}

void LogSummary(const char* why)
{
    const long scored = c_willRagdoll + c_noRig + c_optedOut + c_offScreen + c_lateCrumple + c_overCap;
    const int  pct    = scored ? (int)((100 * c_willRagdoll + scored / 2) / scored) : 0;
    LogF(1, "[stats] %s deaths=%ld ragdolled=%ld/%ld (%d%%) | refused: norig=%ld optedOut=%ld "
            "offScreen=%ld lateCrumple=%ld overCap=%ld | activeNow=%d cap=%s codePatched=%d",
         why, c_exec, c_willRagdoll, scored, pct, c_noRig, c_optedOut, c_offScreen,
         c_lateCrumple, c_overCap, ActiveRagdolls(), CapLabel(), (int)g_bytesPatched);
}

// =============================================================================================
// locating the site (read-only) and, only if asked, editing it
// =============================================================================================
bool Match(const uint8_t* at, const int* pat, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (pat[i] != 0x100 && at[i] != static_cast<uint8_t>(pat[i])) return false;
    return true;
}

const uint8_t* FindOne(const uint8_t* start, size_t len, const int* pat, size_t n, int* count)
{
    const uint8_t* hit = nullptr;
    *count = 0;
    for (size_t i = 0; i + n <= len; ++i)
        if (Match(start + i, pat, n)) { if (!*count) hit = start + i; ++*count; }
    return hit;
}

bool WriteBytes(void* at, const void* src, size_t n)
{
    DWORD old = 0;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, src, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

// 48 8B 05 <disp32>      mov  rax, [rip+disp32]      ; rax = gGameEngine
// 83 B8 <off32> 05       cmp  dword [rax+off32], 5
// 7D <rel8>              jge  -> play a death animation instead
static const int PAT_CAP[] = {
    0x48, 0x8B, 0x05, 0x100, 0x100, 0x100, 0x100,
    0x83, 0xB8, 0x100, 0x100, 0x100, 0x100, 0x100,
    0x7D, 0x100
};
static const int PAT_FLAG[] = {
    0x80, 0xBF, 0x6C, 0x28, 0x00, 0x00, 0x00, 0x74, 0x100
};

void LocateSites(uint8_t* execFn)
{
    const size_t SCAN = 0x600;
    int n = 0;
    const uint8_t* cap = FindOne(execFn, SCAN, PAT_CAP, sizeof(PAT_CAP) / sizeof(int), &n);
    if (!cap || n != 1) {
        LogF(1, "[site] cap site NOT FOUND (%d matches) - the game build changed. The cap cannot "
                "be lifted; nothing has been modified.", n);
        return;
    }
    const int32_t disp = *reinterpret_cast<const int32_t*>(cap + 3);
    g_gGameEngine = reinterpret_cast<uint8_t**>(const_cast<uint8_t*>(cap) + 7 + disp);
    g_activeOff   = *reinterpret_cast<const int32_t*>(cap + 9);
    g_stockCap    = cap[13];
    LogF(1, "[site] cap site at %p | gGameEngine=%p activeRagdolls=+0x%x stockCap=%d (read-only)",
         (void*)cap, (void*)g_gGameEngine, g_activeOff, g_stockCap);

    // The legacy path. Off by default: it modifies the loaded image other plugins scan. Only
    // reach for it if the runtime method ever stops working.
    if (g_cfg.enabled && g_cfg.patchBytes) {
        uint8_t* imm = const_cast<uint8_t*>(cap) + 13;
        uint8_t* jge = const_cast<uint8_t*>(cap) + 14;
        if (g_cfg.maxActiveRagdolls >= 1 && g_cfg.maxActiveRagdolls <= 127) {
            const uint8_t v = static_cast<uint8_t>(g_cfg.maxActiveRagdolls);
            g_bytesPatched = WriteBytes(imm, &v, 1);
            LogF(1, "[patch] LEGACY byte edit: cap %d -> %d (%s)", g_stockCap, (int)v,
                 g_bytesPatched ? "ok" : "FAILED");
        } else {
            const uint8_t nops[2] = { 0x90, 0x90 };
            g_bytesPatched = WriteBytes(jge, nops, 2);
            LogF(1, "[patch] LEGACY byte edit: cap -> unlimited, jge nopped (%s)",
                 g_bytesPatched ? "ok" : "FAILED");
        }
        LogF(1, "[patch] WARNING: Game.dll code is now modified. Other plugins that locate their "
                "targets by scanning the loaded image may stop finding them.");
    }

    if (g_cfg.enabled && g_cfg.ignoreRagdollFlag) {
        if (!g_cfg.patchBytes) {
            LogF(1, "[site] ignoreRagdollFlag needs patchBytes=1 (it has no runtime equivalent); "
                    "the DBR opt-out is still honoured");
        } else {
            int m = 0;
            const uint8_t* flag = FindOne(execFn, SCAN, PAT_FLAG, sizeof(PAT_FLAG) / sizeof(int), &m);
            if (!flag || m != 1) {
                LogF(1, "[patch] ragdollPhysics site NOT FOUND (%d matches)", m);
            } else {
                const uint8_t nops[2] = { 0x90, 0x90 };
                LogF(1, "[patch] ragdollPhysics opt-out ignored (%s)",
                     WriteBytes(const_cast<uint8_t*>(flag) + 7, nops, 2) ? "ok" : "FAILED");
            }
        }
    }
}

// =============================================================================================
// the hook: measurement, and the cap lift
// =============================================================================================
void __cdecl hk_Execute(void* self, bool finishing)
{
    InterlockedIncrement(&c_exec);

    // Counters are deliberately NOT gated on logLevel>=2 - only the per-death LINE is. Tallying
    // the outcome is what makes logLevel=1 useful over a long session, and it is what a control
    // run (enabled=0) needs in order to measure stock behaviour at all.
    if (g_cfg.logLevel >= 1 && self) {
        uint8_t* chr = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(self) + 8);
        if (chr) {
            const bool flag    = chr[GDR_OFF_RAGDOLL_PHYSICS] != 0;
            const bool crumple = chr[GDR_OFF_LATE_CRUMPLE] != 0;
            const bool rig     = p_HasRigidBodyData && p_HasRigidBodyData(chr);
            const bool seen    = p_InFrustum && p_InFrustum(chr);
            const int  active  = ActiveRagdolls();
            const int  cap     = (g_cfg.enabled && g_cfg.liftCap)
                                 ? (g_cfg.maxActiveRagdolls >= 1 ? g_cfg.maxActiveRagdolls : 0x7fffffff)
                                 : g_stockCap;

            const char* why = "RAGDOLL";
            if (!flag)                             { why = "no-ragdollPhysics"; InterlockedIncrement(&c_optedOut); }
            else if (!seen)                        { why = "off-screen";        InterlockedIncrement(&c_offScreen); }
            else if (!rig)                         { why = "no-rig";            InterlockedIncrement(&c_noRig); }
            else if (crumple)                      { why = "doLateCrumple";     InterlockedIncrement(&c_lateCrumple); }
            else if (active >= 0 && active >= cap) { why = "over-cap";          InterlockedIncrement(&c_overCap); }
            else                                   { InterlockedIncrement(&c_willRagdoll); }

            LogF(2, "[death] chr=%p -> %-18s (flag=%d seen=%d rig=%d crumple=%d active=%d cap=%d)",
                 (void*)chr, why, (int)flag, (int)seen, (int)rig, (int)crumple, active,
                 cap == 0x7fffffff ? -1 : cap);
        }
    }

    // Lift the ceiling without touching a byte of Game.dll. The inlined test reads
    // gGameEngine->activeRagdolls, so show it a value that passes, then put the true count back
    // plus whatever the engine added while it ran. Restoring the DELTA rather than the old value
    // keeps the counter honest whether the engine registers the new ragdoll inside this call or
    // after it, so nothing drifts and DecActiveRagdolls stays balanced.
    int* pActive = ActiveRagdollsPtr();
    int  saved   = 0;
    bool spoofed = false;
    if (g_cfg.enabled && g_cfg.liftCap && pActive && !g_bytesPatched) {
        saved = *pActive;
        const int cap = g_cfg.maxActiveRagdolls;
        if (cap < 1 || saved < cap) { *pActive = 0; spoofed = true; }
    }

    o_Execute(self, finishing);

    if (spoofed) *pActive = saved + *pActive;

    if (g_cfg.summaryEvery > 0 && (c_exec % g_cfg.summaryEvery) == 0) LogSummary("periodic");
}

// =============================================================================================
// install
// =============================================================================================
bool Install()
{
    HMODULE hGame   = GetModuleHandleA("Game.dll");
    HMODULE hEngine = GetModuleHandleA("Engine.dll");
    if (!hGame || !hEngine) {
        LogF(1, "[init] FAILED: Game.dll=%p Engine.dll=%p", (void*)hGame, (void*)hEngine);
        return false;
    }

    p_HasRigidBodyData = reinterpret_cast<Gdr_HasRigidBodyData_t>(
        GetProcAddress(hEngine, GDR_ACTOR_HASRIGIDBODYDATA));
    p_InFrustum = reinterpret_cast<Gdr_InRenderPreLoadFrustum_t>(
        GetProcAddress(hEngine, GDR_ENTITY_INRENDERPRELOADFRUSTUM));

    uint8_t* execFn = reinterpret_cast<uint8_t*>(
        GetProcAddress(hGame, GDR_DEFAULTDEATHHANDLER_EXECUTE));
    if (!execFn) {
        LogF(1, "[init] FAILED: %s not found", GDR_DEFAULTDEATHHANDLER_EXECUTE);
        return false;
    }
    LogF(1, "[init] DefaultDeathHandler::Execute=%p HasRigidBodyData=%p InFrustum=%p",
         (void*)execFn, (void*)p_HasRigidBodyData, (void*)p_InFrustum);

    LocateSites(execFn);

    if (MH_Initialize() != MH_OK) { LogF(1, "[init] MH_Initialize failed"); return false; }
    MH_STATUS st = MH_CreateHook(execFn, reinterpret_cast<void*>(&hk_Execute),
                                 reinterpret_cast<void**>(&o_Execute));
    if (st == MH_OK) st = MH_EnableHook(execFn);
    if (st != MH_OK) {
        LogF(1, "[init] Execute hook FAILED (%d) - without it the cap cannot be lifted", (int)st);
        return false;
    }
    LogF(1, "[init] hooked DefaultDeathHandler::Execute");

    LogF(1, "[init] ready | enabled=%d liftCap=%d maxActive=%d patchBytes=%d logLevel=%d | "
            "Game.dll code %s",
         (int)g_cfg.enabled, (int)g_cfg.liftCap, g_cfg.maxActiveRagdolls,
         (int)g_cfg.patchBytes, g_cfg.logLevel,
         g_bytesPatched ? "MODIFIED (legacy byte patch)" : "untouched");

    const char* cmd = GetCommandLineA();
    bool basemods = false;
    for (const char* p = cmd; *p; ++p)
        if ((*p == '/' || *p == '-') && _strnicmp(p + 1, "basemods", 8) == 0) { basemods = true; break; }
    LogF(1, "[init] /basemods %s | cmdline: %s",
         basemods ? "PRESENT (database overlay active)" : "absent (stock skill data)", cmd);
    return true;
}

DWORD WINAPI InitThread(LPVOID)
{
    for (int i = 0; i < 600; ++i) {
        if (GetModuleHandleA("Game.dll") && GetModuleHandleA("Engine.dll")) break;
        Sleep(100);
    }
    Install();
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_logLock);
        g_logLockReady = true;
        LoadConfig(ModuleDir(hModule));
        LogF(1, "==== gd-ragdoll " GDR_VERSION " attach ====");
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        LogSummary("detach");
    }
    return TRUE;
}
