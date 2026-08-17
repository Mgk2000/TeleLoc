@echo off
chcp 65001 > nul
echo === [СБOРКA DESKTOP] Быстрая компиляция под Qt 6.7.2 MinGW...

:: 1. Автоматический поиск папки сборки MinGW от Креатора
set "BUILD_DIR="
for /d %%d in ("D:\Projects\TeleLoc\build\*mingw*") do set "BUILD_DIR=%%d"

if "%BUILD_DIR%"=="" (
    echo [OШИБКA] Не найдена папка сборки MinGW от Qt Creator!
    pause
    exit /b
)

:: 2. Жестко фиксируем пути к вашей рабочей десктопной Qt 6.7.2
set "QT_MINGW_BIN=D:\Qt\6.7.2\mingw_64\bin"
if not exist "%QT_MINGW_BIN%" set "QT_MINGW_BIN=D:\Qt-Desktop-6.7.2\bin"

set "MINGW_TOOLS="
for /d %%t in ("D:\Qt\Tools\mingw*") do set "MINGW_TOOLS=%%t\bin"

set "PATH=%MINGW_TOOLS%;%QT_MINGW_BIN%;%PATH%"

echo === [COMPILING] Сборка изменённого C++ кода...
"D:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%BUILD_DIR%" --target all

if %ERRORLEVEL% NEQ 0 (
    echo [OШИБКA] Компиляция провалилась!
    pause
    exit /b
)

echo === [ПЛAТФOРМA] Копирование графического плагина Windows...
cd /d "%BUILD_DIR%"
if not exist "platforms" mkdir "platforms"
if exist "D:\Qt\6.7.2\mingw_64\plugins\platforms\qwindows.dll" (
    copy /y "D:\Qt\6.7.2\mingw_64\plugins\platforms\qwindows.dll" "platforms\" > nul
)

echo === [ЗАПУСК] Автономный старт рации Анфисы...
:: ИСПРАВЛЕНО: Запускаем приложение в фоновом потоке ОС Windows и намертво тушим батник
start "" "appTeleLoc.exe"
exit
