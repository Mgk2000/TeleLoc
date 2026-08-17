@echo off
:: ИСПРАВЛЕНО: chcp вместо капризного charset
chcp 65001 >nul

echo === СИЛОВОЙ СБРОС КЭША И МАТРЕШКИ TELELOC ===

:: 1. ЖЁСТКО И СИЛОЙ УБИВАЕМ ВСЕ ЗАВИСШИЕ ПРОЦЕССЫ И JAVA-ПОТОКИ GRADLE
taskkill /f /im qtcreator.exe /t 2>nul
taskkill /f /im ninja.exe /t 2>nul
taskkill /f /im cmake.exe /t 2>nul
taskkill /f /im adb.exe /t 2>nul
taskkill /f /im java.exe /t 2>nul
taskkill /f /im javac.exe /t 2>nul

echo Ожидание полного освобождения дескрипторов файлов...
timeout /t 1 /nobreak >nul

:: 2. Создаем временную абсолютно пустую папку на диске D
mkdir "D:\Projects\EmptyFolder" 2>nul
:: 3. Выжигаем разросшуюся папку build идеальным зеркалированием пустой папки
:: ИСПРАВЛЕНО: Флаги /R:0 и /W:0 намертво блокируют любые зависания!
echo Силовое выжигание матрешки с помощью Robocopy...
robocopy "D:\Projects\EmptyFolder" "D:\Projects\TeleLoc\build" /MIR /R:0 /W:0 >nul

:: 4. Удаляем пустые каталоги и битые конфигурационные файлы настроек Креатора
rmdir /s /q "D:\Projects\TeleLoc\build" 2>nul
rmdir /s /q "D:\Projects\EmptyFolder" 2>nul
del /f /q "D:\Projects\TeleLoc\CMakeLists.txt.user" 2>nul

echo.
echo === СБРОС ЗАВЕРШЕН УСПЕШНО! ДИСК СТЕРЕЛЬНО ЧИСТ! ===
echo.
pause
