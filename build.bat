@echo off
rem Builds build\dinput8.dll (32-bit, static CRT) with the VS 2019 C++ toolset.
setlocal
cd /d "%~dp0"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo vcvars32.bat not found at "%VCVARS%"
    exit /b 1
)
call "%VCVARS%" >nul 2>nul

if not exist build mkdir build

cl /nologo /std:c++17 /O2 /MT /W4 /EHsc /Zi /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
   /Fobuild\ /Fdbuild\ /Febuild\dinput8.dll src\*.cpp /LD ^
   /link /DEF:src\dinput8.def /PDB:build\dinput8.pdb /PDBALTPATH:dinput8.pdb /MAP:build\dinput8.map d3d10.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1

echo Built build\dinput8.dll
