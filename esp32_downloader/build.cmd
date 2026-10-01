@echo off
setlocal

rem ============================================================
rem  ESP32-C3 OTA downloader - build / flash / monitor
rem  Works without the VSCode ESP-IDF extension (env set here).
rem
rem  Usage:
rem    build.cmd                 -> build only
rem    build.cmd all  [COMx]     -> build + flash + monitor
rem    build.cmd flash [COMx]    -> flash only
rem    build.cmd monitor [COMx]  -> serial monitor only (Ctrl+] to exit)
rem    build.cmd erase [COMx]    -> erase flash
rem  Default port: COM11
rem ============================================================

set "IDF_PATH=D:\Espressif\frameworks\esp-idf-v5.4.4"
set "IDF_TOOLS_PATH=D:\Espressif"
set "IDF_PYTHON_ENV_PATH=D:\Espressif\python_env\idf5.4_py3.11_env"

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=build"
set "ESP_PORT=%~2"
if "%ESP_PORT%"=="" set "ESP_PORT=COM11"

set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%IDF_TOOLS_PATH%\tools\cmake\3.30.2\bin;%IDF_TOOLS_PATH%\tools\ninja\1.12.1;%IDF_TOOLS_PATH%\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;%IDF_TOOLS_PATH%\tools\idf-git\2.44.0\cmd;%PATH%"

cd /d "%~dp0"
set IDFPY=python.exe "%IDF_PATH%\tools\idf.py"

rem --- self-heal: a build folder left half-configured by a failed CMake run ---
if exist "build\CMakeCache.txt" if not exist "build\build.ninja" (
    echo [INFO] Broken build folder found, removing it ...
    rmdir /s /q "build"
)

if /i "%ACTION%"=="build"   goto :do_build
if /i "%ACTION%"=="all"     goto :do_all
if /i "%ACTION%"=="flash"   goto :do_flash
if /i "%ACTION%"=="monitor" goto :do_monitor
if /i "%ACTION%"=="erase"   goto :do_erase
echo [ERROR] Unknown action: %ACTION%
goto :usage

:do_build
echo [1/1] Building ...
%IDFPY% build
goto :done

:do_all
echo [1/3] Building ...
%IDFPY% build || goto :fail
echo [2/3] Flashing to %ESP_PORT% ...
%IDFPY% -p %ESP_PORT% flash || goto :fail
echo [3/3] Monitor on %ESP_PORT% - press Ctrl+] to exit ...
%IDFPY% -p %ESP_PORT% monitor
goto :done

:do_flash
echo Flashing to %ESP_PORT% ...
%IDFPY% -p %ESP_PORT% flash
goto :done

:do_monitor
echo Monitor on %ESP_PORT% - press Ctrl+] to exit ...
%IDFPY% -p %ESP_PORT% monitor
goto :done

:do_erase
echo Erasing flash on %ESP_PORT% ...
%IDFPY% -p %ESP_PORT% erase-flash
goto :done

:usage
echo.
echo   build.cmd                 build only
echo   build.cmd all  [COMx]     build + flash + monitor
echo   build.cmd flash [COMx]    flash only
echo   build.cmd monitor [COMx]  serial monitor only
echo   build.cmd erase [COMx]    erase flash
echo.
goto :done

:fail
echo.
echo [ERROR] Command failed, see messages above.
pause
exit /b 1

:done
echo.
echo Done.
pause
