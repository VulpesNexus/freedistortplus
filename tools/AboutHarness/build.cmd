@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Vixen420
rem
rem Builds the About-dialog harness. Development tool; not part of the plugin.
rem Needs a Visual Studio command prompt, or run it through vcvars64.bat.
rem
rem   build.cmd            build it
rem   AboutHarness.exe     show the dialog
rem   AboutHarness.exe /dark       show it in Illustrator's darkest colors
rem   AboutHarness.exe /exit3000   show it, then close after 3 seconds

setlocal
cd /d "%~dp0"

where cl >nul 2>&1
if errorlevel 1 (
  echo cl.exe not found. Run this from a Visual Studio x64 command prompt.
  exit /b 1
)

set SRC=..\..\plugin\Source
set RES=..\..\plugin\Resources\Win

rc /nologo /I "%RES%" /I "%SRC%" /fo harness.res harness.rc || exit /b 1

cl /nologo /EHsc /W4 /std:c++17 /DUNICODE /D_UNICODE /DWIN_ENV ^
   /I "%SRC%" /I "%RES%" ^
   main.cpp "%SRC%\FDPAbout.cpp" harness.res ^
   /Fe:AboutHarness.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shell32.lib || exit /b 1

del /q *.obj harness.res 2>nul
echo Built AboutHarness.exe
