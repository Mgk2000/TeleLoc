cmake_minimum_required(VERSION 3.16)

project(TeleLoc VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Автоматическое добавление разрешения на ИНТЕРНЕТ для Android
set(QT_ANDROID_INTERNET_PERMISSION ON CACHE BOOL "Auto internet for Android" FORCE)

# Правильный, безопасный синтаксис кодировок для MSVC и Clang
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-fexec-charset=UTF-8)
endif()

# Находим компоненты Qt 6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network Multimedia)

# Создаем исполняемый файл
qt_add_executable(appTeleLoc WIN32
    main.cpp
    networkengine.cpp networkengine.h
    audioengine.cpp audioengine.h
)
if(ANDROID)
    set_property(TARGET appTeleLoc PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
endif()

# QML-модуль
qt_add_qml_module(appTeleLoc
    URI TeleLoc
    VERSION 1.0
     RESOURCE_PREFIX "/qt/qml"
    QML_FILES Main.qml
    RESOURCES
        ringback.wav
        busy.wav
        ringtone.wav
)

# Линковка библиотек
target_link_libraries(appTeleLoc PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network Qt6::Multimedia
)
if(ANDROID)
    set(ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
endif()

# Включаем текущую директорию для поиска заголовочных файлов
target_include_directories(appTeleLoc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
