// gdr.h - Grim Dawn "always ragdoll" mod: engine symbols and structures.
//
// Grim Dawn 1.3.0.8 x64. Game.dll exports 25,100 decorated C++ symbols and Engine.dll 6,283, so
// everything below is resolved with GetProcAddress by mangled name - no byte-pattern scanning for
// function entry points and no build-specific addresses.
//
// ABI: all non-static member functions, Microsoft x64, so the object pointer is the first
// argument. On x64 __cdecl/__thiscall/__stdcall collapse to the same convention. None of these
// return a class by value, so no sret slot is involved.
#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------------------------
// Where the death/ragdoll decision really is
// ---------------------------------------------------------------------------------------------
// Character::ShouldDoRagDoll and GameEngine::AllowRagdolls are exported and read like the obvious
// hook points:
//
//     bool Character::ShouldDoRagDoll() const     // Game.dll +0x6E110
//         return ragdollPhysics && Entity::InRenderPreLoadFrustum() && Actor::HasRigidBodyData();
//     bool GameEngine::AllowRagdolls() const      // Game.dll +0x2D9B80
//         return activeRagdolls < 5;              // kMaxActiveRagdolls = 5
//
// They are NOT the hook points. A measurement run over 198 real deaths recorded zero calls to
// either: the compiler inlined both into DefaultDeathHandler::Execute and left the out-of-line
// copies unreferenced. ShouldDoRagDoll has exactly one static caller in the whole binary,
// DefaultDeathHandler::AnimationCallback, which is the separate "late crumple" path.
//
// The branch the engine actually runs, inlined at DefaultDeathHandler::Execute + ~0x268:
//
//     chr->ragdollPhysics                     cmp byte [rdi+0x286C], 0    je  -> animation
//     && Entity::InRenderPreLoadFrustum(chr)  call                        je  -> animation
//     && Actor::HasRigidBodyData(chr)         call                        je  -> animation
//     && chr->doLateCrumple == 0              cmp byte [rcx+0x286D], 0    jne -> animation
//     && gGameEngine->activeRagdolls < 5      cmp dword [rax+0x37690], 5  jge -> animation
//
// A ragdoll is otherwise pushed onto the victim by the killing attack: CombatManager::TakeAttack
// calls Character::SetRagdollData with a PhysicsMotion built from the attack's ragDoll* fields.

// public: virtual void __cdecl GAME::DefaultDeathHandler::Execute(bool) __ptr64
// THE function: holds the inlined branch above. Its Character is at DeathHandler + 8.
#define GDR_DEFAULTDEATHHANDLER_EXECUTE "?Execute@DefaultDeathHandler@GAME@@UEAAX_N@Z"
// public: bool __cdecl GAME::Actor::HasRigidBodyData(void) const __ptr64            [Engine.dll]
#define GDR_ACTOR_HASRIGIDBODYDATA      "?HasRigidBodyData@Actor@GAME@@QEBA_NXZ"
// public: bool __cdecl GAME::Entity::InRenderPreLoadFrustum(void) const __ptr64     [Engine.dll]
#define GDR_ENTITY_INRENDERPRELOADFRUSTUM "?InRenderPreLoadFrustum@Entity@GAME@@QEBA_NXZ"

// Kept for reference and for anyone re-checking the above - both are inlined away in 1.3.0.8 and
// hooking them does nothing. Do not build behaviour on them without re-measuring first.
#define GDR_CHARACTER_SHOULDDORAGDOLL   "?ShouldDoRagDoll@Character@GAME@@QEBA_NXZ"
#define GDR_GAMEENGINE_ALLOWRAGDOLLS    "?AllowRagdolls@GameEngine@GAME@@QEBA_NXZ"
// public: void __cdecl GAME::Character::SetRagdollData(struct GAME::PhysicsMotion const &, bool)
#define GDR_CHARACTER_SETRAGDOLLDATA    "?SetRagdollData@Character@GAME@@QEAAXAEBUPhysicsMotion@2@_N@Z"

// ---------------------------------------------------------------------------------------------
// GAME::PhysicsMotion - 28 bytes, lives inline in Character at +0x2850.
//
// Layout read off DefaultDeathHandler::AnimationCallback, which builds one from the 16-byte
// constant {0.0f, 1.0f, 0.0f, 1.0f} at Game.dll rva 0x777340 with a zero int on each end, then
// overwrites +0x00 with PhysicsUtil::GetEffectEnum(overrideRagdollBehavior) and +0x10 with
// overrideRagdollSpeed when those DBR fields are set. Character::GetRagdollData is literally
// `lea rax,[rcx+0x2850]; ret`, fixing both the offset and the 0x1C size.
// ---------------------------------------------------------------------------------------------
struct GdrPhysicsMotion {
    int32_t type;      // +0x00  PhysicsMotion::Type - Crumple / TakeHit / Random, 0 = default
    float   dirX;      // +0x04
    float   dirY;      // +0x08  engine default 1.0 (straight up)
    float   dirZ;      // +0x0C
    float   speed;     // +0x10  engine default 1.0; overrideRagdollSpeed replaces it when != 0
    float   unk14;     // +0x14  engine default 0.0
    int32_t unk18;     // +0x18  engine default 0
};
static_assert(sizeof(GdrPhysicsMotion) == 0x1C, "PhysicsMotion must be 28 bytes");

// Character field offsets, from the DBR loader at Game.dll +0x56151..+0x561F8.
// `doLateCrumple` and `physicsTimeLimit` are parsed by the engine but are NOT in character.tpl.
#define GDR_OFF_OVERRIDE_BEHAVIOR 0x2810   // std::string  overrideRagdollBehavior
#define GDR_OFF_OVERRIDE_SPEED    0x2830   // float        overrideRagdollSpeed
#define GDR_OFF_RAGDOLL_DATA      0x2850   // PhysicsMotion
#define GDR_OFF_RAGDOLL_PHYSICS   0x286C   // bool         ragdollPhysics      (default 1)
#define GDR_OFF_LATE_CRUMPLE      0x286D   // bool         doLateCrumple       (undocumented)
#define GDR_OFF_PHYSICS_TIMELIMIT 0x2870   // int          physicsTimeLimit    (undocumented)

typedef void (*Gdr_Execute_t)(void* deathHandler, bool finishing);
typedef bool (*Gdr_HasRigidBodyData_t)(void* actor);
typedef bool (*Gdr_InRenderPreLoadFrustum_t)(void* entity);
typedef void (*Gdr_SetRagdollData_t)(void* character, const GdrPhysicsMotion* motion, bool force);
