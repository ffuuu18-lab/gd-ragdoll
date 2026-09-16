@echo off
rem ============================================================================================
rem  build.bat - gd-ragdoll
rem
rem  Produces bin\ragdoll.asi : x64 Release, /MD. Needs the MSVC 2022 C++ build tools; nothing
rem  else. tools\find_vcvars.bat locates them through vswhere, so no path is written down here.
rem
rem  /MD is required, not a preference: the game uses the VC14x runtime, so anything shared with
rem  Engine.dll or Game.dll has to come from the same CRT.
rem
rem  /d2FH4- keeps the compiler on the FH3 exception tables. FH4 is MSVC 2019+'s compressed format
rem  and costs an import of VCRUNTIME140_1.dll, the youngest corner of the runtime surface and the
rem  one least likely to be present under Proton. Windows behaviour is identical; only the table
rem  encoding differs.
rem
rem  The .asi extension is what Ultimate ASI Loader looks for: it LoadLibrary's every *.asi beside
rem  the exe. The file is an ordinary DLL - it exports nothing and only DllMain runs.
rem ============================================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"

call "%ROOT%tools\find_vcvars.bat"
if errorlevel 1 (
    echo [build] ERROR: no x64 C++ toolchain - see the message above
    exit /b 1
)
if not defined VSCMD_ARG_TGT_ARCH call "%VCVARS%" >nul
if errorlevel 1 (
    echo [build] ERROR: vcvars64.bat failed
    exit /b 1
)

set "OBJ=%ROOT%build\obj"
set "BIN=%ROOT%bin"
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%BIN%" mkdir "%BIN%"
del /q "%OBJ%\*.obj" 2>nul

set "MINHOOK=%ROOT%third_party\minhook"
if not exist "%MINHOOK%\include\MinHook.h" (
    echo [build] ERROR: MinHook missing at "%MINHOOK%"
    exit /b 1
)

rem ---- 1. MinHook (C, third-party: no /WX) ---------------------------------------------------
echo [build] MinHook...
cl /nologo /c /O2 /MD /W3 /GS- /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%MINHOOK%\include" /Fo"%OBJ%\\" ^
   "%MINHOOK%\src\buffer.c" "%MINHOOK%\src\hook.c" "%MINHOOK%\src\trampoline.c" ^
   "%MINHOOK%\src\hde\hde64.c"
if errorlevel 1 goto :fail

rem ---- 2. the mod ----------------------------------------------------------------------------
echo [build] mod sources...
cl /nologo /c /O2 /MD /W4 /WX /EHsc /std:c++17 /GR- /d2FH4- /DNDEBUG ^
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /I"%MINHOOK%\include" /I"%ROOT%src" /Fo"%OBJ%\\" ^
   "%ROOT%src\dllmain.cpp"
if errorlevel 1 goto :fail

rem ---- 3. link -------------------------------------------------------------------------------
rem Symbols on every build: a crash report is unreadable without the rva -> function map, and by
rem the time one arrives a rebuild has usually replaced the binary that produced it.
rem
rem /PDBALTPATH:%%_PDB%% is not optional for anything published. By default the linker stamps the
rem FULL path of the .pdb into the binary's debug directory, so a shipped DLL carries the build
rem machine's directory layout - drive letter, folder names and all. %%_PDB%% substitutes just the
rem file name, which is all a debugger needs when the .pdb sits next to the binary.
rem tools\audit_release.py checks that this actually took effect.
echo [build] link...
link /nologo /DLL /OUT:"%BIN%\ragdoll.asi" /MACHINE:X64 /DEBUG /OPT:REF /OPT:ICF ^
     /PDBALTPATH:%%_PDB%% ^
     /MAP:"%BIN%\ragdoll.map" /PDB:"%BIN%\ragdoll.pdb" ^
     "%OBJ%\*.obj" kernel32.lib user32.lib
if errorlevel 1 goto :fail

echo.
echo [build] OK -^> %BIN%\ragdoll.asi
for %%F in ("%BIN%\ragdoll.asi") do echo [build]    %%~zF bytes
echo [build] install with:  powershell -ExecutionPolicy Bypass -File deploy.ps1
exit /b 0

:fail
echo.
echo [build] FAILED
exit /b 1
