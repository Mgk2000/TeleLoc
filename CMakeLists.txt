cmake_minimum_required(VERSION 3.16)

project(TeleLoc VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Оптимизация памяти компилятора: режем тяжелые отладочные символы для Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-g1 -fno-limit-debug-info)
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    Network
    Multimedia
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Настройка локальной сборки Opus строго без тестов и лишних утилит
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_CUSTOM_MODES ON CACHE BOOL "" FORCE)
add_subdirectory(opus)

qt_add_executable(appTeleLoc
    main.cpp
    networkengine.h
    networkengine.cpp
    audioengine.h
    audioengine.cpp
)

qt_add_qml_module(appTeleLoc
    URI TeleLoc
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES Main.qml
    CallScreenPopup.qml
    RESOURCES
        ringtone.wav
        ringback.wav
        busy.wav
        RESOURCES android/src/org/qtproject/example/appTeleLoc/TeleLocService.java
        RESOURCES android/src/org/qtproject/example/appTeleLoc/TeleLocWakeReceiver.java
        RESOURCES qtquickcontrols2.conf
)

# Прописываем только ПУБЛИЧНЫЕ заголовочные файлы Opus
target_include_directories(appTeleLoc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/opus/include
)

target_link_libraries(appTeleLoc PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::Multimedia
    opus
)

if(WIN32)
    target_compile_definitions(appTeleLoc PRIVATE WIN32)
endif()

if(ANDROID)
    set_property(TARGET appTeleLoc PROPERTY
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()
