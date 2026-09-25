@echo off
setlocal
set "ITEMSET_ROOT=%~dp0"
set "ITEMSET_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%ITEMSET_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%i in (`"%ITEMSET_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ITEMSET_VS=%%i"
if not defined ITEMSET_VS exit /b 1
call "%ITEMSET_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%ITEMSET_ROOT%build" mkdir "%ITEMSET_ROOT%build"
if not defined ITEMSET_PYTHON set "ITEMSET_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if not exist "%ITEMSET_PYTHON%" set "ITEMSET_PYTHON=python"
"%ITEMSET_PYTHON%" -B "%ITEMSET_ROOT%tools\check_native_api.py"
if errorlevel 1 exit /b 1
"%ITEMSET_PYTHON%" -B "%ITEMSET_ROOT%tools\build_set_catalog.py"
if errorlevel 1 exit /b 1
"%ITEMSET_PYTHON%" -B "%ITEMSET_ROOT%tests\test_set_catalog.py"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ITEMSET_ROOT%tests\test_update_config.ps1"
if errorlevel 1 exit /b 1
pushd "%ITEMSET_ROOT%build"
cl /nologo /std:c++17 /EHsc /MT /utf-8 /O2 /W3 /I"%ITEMSET_ROOT%vendor\mewjector" /LD "%ITEMSET_ROOT%src\mod.cpp" "%ITEMSET_ROOT%src\native_tooltip.cpp" "%ITEMSET_ROOT%src\safe_read.cpp" "%ITEMSET_ROOT%src\panel_content.cpp" /Fe:ItemSetInfo.dll /link user32.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++17 /EHsc /MT /utf-8 /O2 /W3 "%ITEMSET_ROOT%tests\panel_content_tests.cpp" "%ITEMSET_ROOT%src\panel_content.cpp" "%ITEMSET_ROOT%src\safe_read.cpp" /Fe:panel_content_tests.exe
if errorlevel 1 goto :failed
panel_content_tests.exe
if errorlevel 1 goto :failed
popd
exit /b 0
:failed
popd
exit /b 1
