@echo off
setlocal EnableExtensions

rem ---------------------------------------------------------------------------
rem Build, and optionally run, TagComposer.
rem
rem   build.cmd              build Release
rem   build.cmd run          build Release, then launch the GUI
rem   build.cmd check        build Release, then run the check suite
rem   build.cmd debug        build Debug
rem   build.cmd debug run    build Debug, then launch the GUI
rem   build.cmd fresh        re-run CMake before building
rem   build.cmd clean        delete the build dir, then configure + build
rem
rem Overridable before the call: QTDIR, VCVARS, and DATADIR (the data folder
rem for run/check, default ..\out\build\release\data).
rem ---------------------------------------------------------------------------

for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "CONFIG=Release"
set "BUILDDIR=release"
set "DORUN=0"
set "DOCHECK=0"
set "FRESH=0"
set "CLEAN=0"

:parseargs
if "%~1"=="" goto parsed
if /i "%~1"=="run"     set "DORUN=1"
if /i "%~1"=="check"   set "DOCHECK=1"
if /i "%~1"=="fresh"   set "FRESH=1"
if /i "%~1"=="clean"   set "CLEAN=1"
if /i "%~1"=="debug"   set "CONFIG=Debug"
if /i "%~1"=="debug"   set "BUILDDIR=debug"
if /i "%~1"=="release" set "CONFIG=Release"
if /i "%~1"=="release" set "BUILDDIR=release"
shift
goto parseargs
:parsed

set "BUILD=%ROOT%\out\build\%BUILDDIR%"
if not defined QTDIR set "QTDIR=C:/Qt/6.11.0/msvc2022_64"
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

rem ---- MSVC environment. vcvars appends to PATH on every call, so do it once.
if not defined VSCMD_ARG_TGT_ARCH (
    if not exist "%VCVARS%" (
        echo ERROR: vcvars64.bat not found at:
        echo        %VCVARS%
        echo        Set VCVARS to your Visual Studio location and re-run.
        exit /b 1
    )
    rem Quiet: vcvars chatters about vswhere on some installs even when it works.
    call "%VCVARS%" >nul 2>&1
    where cl >nul 2>&1
    if errorlevel 1 (
        echo ERROR: ran vcvars64.bat but cl.exe is still not on PATH.
        echo        Run it by hand to see why:
        echo        "%VCVARS%"
        exit /b 1
    )
)

rem ---- CMake: prefer one on PATH, else the copy Visual Studio ships.
set "CMAKE=cmake"
where cmake >nul 2>&1 || set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not "%CMAKE%"=="cmake" if not exist "%CMAKE%" (
    echo ERROR: no cmake on PATH and none bundled with Visual Studio.
    exit /b 1
)

if "%CLEAN%"=="1" if exist "%BUILD%" (
    echo == Removing %BUILD%
    rmdir /s /q "%BUILD%"
)

rem ---- A running exe blocks the link (LNK1104); test this exact file.
if exist "%BUILD%\tagcomposer.exe" (
    2>nul ( >>"%BUILD%\tagcomposer.exe" call ) || (
        echo ERROR: this build's tagcomposer.exe is running and holds a lock on
        echo        the binary:
        echo        %BUILD%\tagcomposer.exe
        echo        Close it, then re-run this script.
        exit /b 1
    )
)

rem ---- Configure.
if not exist "%BUILD%\CMakeCache.txt" set "FRESH=1"
if "%FRESH%"=="1" (
    echo == Configuring %CONFIG%
    "%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH="%QTDIR%"
    if errorlevel 1 (
        echo ERROR: configure failed.
        exit /b 1
    )
)

rem ---- Build. New source files must be added to CMakeLists.txt.
echo == Building %CONFIG%
"%CMAKE%" --build "%BUILD%" --parallel
if errorlevel 1 (
    echo ERROR: build failed.
    exit /b 1
)

set "EXE=%BUILD%\tagcomposer.exe"
set "CHECKEXE=%BUILD%\tc_checks.exe"
if not exist "%EXE%" (
    echo ERROR: build reported success but %EXE% is missing.
    exit /b 1
)

echo.
echo Built: %EXE%
echo        %CHECKEXE%

if not defined DATADIR set "DATADIR=%ROOT%\..\out\build\release\data"

rem ---- Checks run before launching.
if "%DOCHECK%"=="1" (
    echo == Checks
    echo.
    "%CHECKEXE%" "%DATADIR%\system" "%DATADIR%\entry"
    if errorlevel 1 (
        echo.
        echo ERROR: checks failed.
        exit /b 1
    )
)

rem ---- Launch detached from this console.
if "%DORUN%"=="1" (
    echo == Launching
    start "TagComposer" "%EXE%" "%DATADIR%"
)

exit /b 0
