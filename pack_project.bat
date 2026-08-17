@echo off
chcp 65001 > nul
set "OUTPUT_FILE=teleloc_bundle.txt"

echo [System] Сборка кода проекта TeleLoc...

:: Очищаем старый файл, если он существовал
if exist "%OUTPUT_FILE%" del "%OUTPUT_FILE%"

:: Список файлов проекта в строгом порядке
set "FILES=CMakeLists.txt main.cpp Main.qml networkengine.h networkengine.cpp audioengine.h audioengine.cpp android\AndroidManifest.xml android\src\org\qtproject\example\appteleloc\TeleLocService.java  android\src\org\qtproject\example\appteleloc\TeleLocWakeReceiver.java"

:: Цикл по всем файлам из списка
for %%F in (%FILES%) do (
    if exist "%%F" (
        echo ==================================================== >> "%OUTPUT_FILE%"
        echo === FILE: %%F >> "%OUTPUT_FILE%"
        echo ==================================================== >> "%OUTPUT_FILE%"
        echo. >> "%OUTPUT_FILE%"
        type "%%F" >> "%OUTPUT_FILE%"
        echo. >> "%OUTPUT_FILE%"
        echo. >> "%OUTPUT_FILE%"
    ) else (
        echo [Предупреждение] Файл %%F не найден в этой папке!
    )
)

:: Копируем полученный результат в буфер обмена Windows
type "%OUTPUT_FILE%" | clip

echo ====================================================
echo [Успех] Все файлы объединены в %OUTPUT_FILE%
echo [Успех] Содержимое ОЖЕ скопировано в буфер обмена!
echo Теперь просто зайдите на Gist и нажмите Ctrl+V.
echo ====================================================
pause
