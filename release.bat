@echo off
set "GAME_EXE=TS2"
cd /d "%~dp0"
set "OUT=out\build\release"
rem the Visual C++ runtime beside the executable, so a fresh Windows install needs nothing more
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
for /d %%v in ("%VSDIR%\VC\Redist\MSVC\1*") do for /d %%c in ("%%v\x64\Microsoft.VC*.CRT") do set "VCRUNTIME=%%c"
if not exist "%VCRUNTIME%\vcruntime140.dll" (echo Visual C++ runtime not found: install Visual Studio with the C++ workload & exit /b 1)
if not exist "%OUT%\%GAME_EXE%.exe" call build.bat || exit /b 1
rem the configure step writes where the SDK is
if not exist "%OUT%\sdk_dir.txt" cmake --preset release >nul || exit /b 1
if not exist "%OUT%\sdk_dir.txt" echo %OUT%\sdk_dir.txt was not written, update the SDK. && exit /b 1
set /p SDK=<out\build\release\sdk_dir.txt
del /q "%GAME_EXE%.zip" 2>nul
rem an empty assets folder beside the exe, where the game (files or .iso) goes
rd /s /q "%OUT%\release" 2>nul
mkdir "%OUT%\release\assets"
rem the game code DLL, linked against this executable and newer than it (the executable takes an older one for stale and
rem would look for a compiler): built now, and the copy that ships is touched below
set "CI=1"
call "%~dp0build.bat" >nul || exit /b 1
if not exist "%OUT%\%GAME_EXE%.dll" (echo %OUT%\%GAME_EXE%.dll was not built & exit /b 1)
rem Windows' own tar writes the zip (no 7-Zip needed): a staging folder with exactly what ships
set "STAGE=%OUT%\stage"
rd /s /q "%STAGE%" 2>nul
mkdir "%STAGE%\assets"
copy /y "%OUT%\%GAME_EXE%.exe" "%STAGE%\" >nul
copy /y "%OUT%\TS2Launcher.exe" "%STAGE%\" >nul
for %%f in (README.md CREDITS.md) do copy /y "%%f" "%STAGE%\" >nul
for %%f in (hd_textures.bin hd_hud.bin button_icons.bin weapon_icons.bin) do copy /y "%OUT%\%%f" "%STAGE%\" >nul
copy /y "%SDK%\THIRD_PARTY.txt" "%STAGE%\" >nul
rem the shaders and pipeline keys of the places played here, as seeds: a new player's run builds them ahead instead of stopping for them in play
if exist "%OUT%\shaders.bin" copy /y "%OUT%\shaders.bin" "%STAGE%\shaders_seed.bin" >nul
if exist "%OUT%\pipekeys.bin" copy /y "%OUT%\pipekeys.bin" "%STAGE%\pipekeys_seed.bin" >nul
for %%f in (msvcp140 vcruntime140 vcruntime140_1) do copy /y "%VCRUNTIME%\%%f.dll" "%STAGE%\" >nul
robocopy "%OUT%\art" "%STAGE%\art" /e /njh /njs /nfl /ndl >nul
robocopy "%OUT%\lang" "%STAGE%\lang" /e /njh /njs /nfl /ndl >nul
robocopy "%OUT%\resourcepacks\beta_weapons" "%STAGE%\resourcepacks\beta_weapons" /e /njh /njs /nfl /ndl >nul
copy /y "%OUT%\%GAME_EXE%.dll" "%STAGE%\" >nul
rem the copy is dated now, after the executable's: the zip keeps that order
powershell -NoProfile -Command "(Get-Item -LiteralPath '%STAGE%\%GAME_EXE%.dll').LastWriteTime = (Get-Date).AddSeconds(30)" || exit /b 1
tar -a -c -f "%GAME_EXE%.zip" -C "%STAGE%" . || exit /b 1
rd /s /q "%STAGE%"
for %%z in ("%GAME_EXE%.zip") do echo %GAME_EXE%.zip: %%~zz bytes
