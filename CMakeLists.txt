cmake_minimum_required(VERSION 3.21)
project(Pulsify VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia Network)

add_executable(Pulsify WIN32 MACOSX_BUNDLE
    main.cpp
    widgets.h
    library.h
    online.h
    playlists.h
    google.h
    importdialog.h
    relay.h
    infopanel.h
    ui.h
)

target_link_libraries(Pulsify PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network)
target_compile_options(Pulsify PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
