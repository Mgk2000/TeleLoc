cmake_minimum_required(VERSION 3.16)

project(TeleLoc VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# АВТО-ИНТЕРНЕТ: Флаг жестко вшит в код сборщика, галочки больше не слетят!
set(QT_ANDROID_INTERNET_PERMISSION ON CACHE BOOL "Auto internet for Android" FORCE)

add_compile_options("$<$<CXX_COMPILER_ID:Clang>:-fexec-charset=UTF-8>")

# Подключаем сеть и мобильный звук
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network Multimedia)

qt_add_executable(appTeleLoc
    main.cpp
    networkengine.cpp networkengine.h
    audioengine.cpp audioengine.h
)

qt_add_qml_module(appTeleLoc
    URI TeleLoc
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(appTeleLoc PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network Qt6::Multimedia
)

target_include_directories(appTeleLoc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
