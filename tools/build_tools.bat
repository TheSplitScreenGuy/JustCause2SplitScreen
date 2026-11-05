@echo off
rem Builds tools\bin\memscan.exe (64-bit, so large scans fit in memory) with the VS 2019 C++ toolset.
setlocal
cd /d "%~dp0"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo vcvars64.bat not found at "%VCVARS%"
    exit /b 1
)
call "%VCVARS%" >nul 2>nul

if not exist bin mkdir bin
if not exist bin\obj mkdir bin\obj

cl /nologo /std:c++17 /O2 /MT /W4 /EHsc /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
   /Fobin\obj\ /Febin\memscan.exe memscan\memscan.cpp /link psapi.lib
if errorlevel 1 exit /b 1

echo Built tools\bin\memscan.exe
