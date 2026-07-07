cmake_minimum_required(VERSION 3.16)

project(TeleLoc VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# СРЕДСТВО ОТ КРЯКОЗЯБРОВ: Заставляем Clang под Android принудительно читать UTF-8!
add_compile_options("$<$<CXX_COMPILER_ID:Clang>:-fexec-charset=UTF-8>")

# Подключаем сеть и графику ядра
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network)

qt_add_executable(appTeleLoc
    main.cpp
    networkengine.cpp networkengine.h
)

qt_add_qml_module(appTeleLoc
    URI TeleLoc
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(appTeleLoc PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network
)
