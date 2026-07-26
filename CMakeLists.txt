cmake_minimum_required(VERSION 3.16)
project(TeleLoc VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-fexec-charset=UTF-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network Multimedia)

# Обычный стандартный таргет без ручной финализации
qt_add_executable(appTeleLoc WIN32
    main.cpp
    networkengine.cpp networkengine.h
    audioengine.cpp audioengine.h
    wifidirectengine.cpp wifidirectengine.h
)

qt_policy(SET QTP0001 NEW)
qt_add_qml_module(appTeleLoc
    URI TeleLoc
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml/"
    QML_FILES Main.qml
    RESOURCES ring1.wav ring2.wav
)

target_link_libraries(appTeleLoc PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network Qt6::Multimedia
)

target_include_directories(appTeleLoc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Жестко связываем сгенерированный Креатором манифест со сборщиком Gradle
if(ANDROID)
    set_target_properties(appTeleLoc PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()
