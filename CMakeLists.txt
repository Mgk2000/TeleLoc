cmake_minimum_required(VERSION 3.16)

project(TeleLoc VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Поиск необходимых модулей Qt 6
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    Network
    Multimedia
)

# Автоматическая генерация мета-объектов MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Определение исполняемого файла приложения (Java-файлы сюда писать НЕЛЬЗЯ!)
qt_add_executable(appTeleLoc
    main.cpp
    networkengine.h
    networkengine.cpp
    audioengine.h
    audioengine.cpp
)
# Регистрация QML-модуля с жестким указанием единого пути ресурсов TeleLoc
qt_add_qml_module(appTeleLoc
    URI TeleLoc
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES Main.qml
    RESOURCES
        ringtone.wav
        ringback.wav
        busy.wav
        RESOURCES android/src/org/qtproject/example/appTeleLoc/TeleLocService.java
        RESOURCES android/src/org/qtproject/example/appTeleLoc/TeleLocService.java
        RESOURCES android/src/org/qtproject/example/appTeleLoc/TeleLocService.java
        RESOURCES android/src/org/qtproject/example/appTeleLoc/TeleLocWakeReceiver.java
        RESOURCES android/src/org/qtproject/example/appTeleLoc/TeleLocService.java
)

# Линковка системных библиотек Qt
target_link_libraries(appTeleLoc PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::Multimedia
)

# Специфичные настройки для Windows (средство от крякозябр)
if(WIN32)
    target_compile_definitions(appTeleLoc PRIVATE WIN32)
endif()

# Специфичные настройки для Android
if(ANDROID)
    set_property(TARGET appTeleLoc PROPERTY
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()
