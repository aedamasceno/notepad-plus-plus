cmake_minimum_required(VERSION 3.24)
project(npp_linux LANGUAGES CXX)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Enable Qt meta‑object compilation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ------------------------------------------------------------------
# Gather all Scintilla core sources (C and C++)
file(GLOB_RECURSE SCINTILLA_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/src/*.cpp"
)

# Gather the Qt wrapper sources for the widget base
file(GLOB_RECURSE QT_EDIT_BASE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEditBase/*.cpp"
)
file(GLOB_RECURSE QT_EDIT_BASE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEditBase/*.h"
)

# Gather the Qt wrapper sources for the widget itself
file(GLOB_RECURSE QT_EDIT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEdit/*.cpp"
)
file(GLOB_RECURSE QT_EDIT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEdit/*.h"
)

# ------------------------------------------------------------------
add_executable(npp_linux main.cpp
    ${SCINTILLA_CORE_SOURCES}
    ${QT_EDIT_BASE_SOURCES}
    ${QT_EDIT_BASE_HEADERS}
    ${QT_EDIT_SOURCES}
    ${QT_EDIT_HEADERS}
)

target_include_directories(npp_linux PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEditBase"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEdit"
)

target_link_libraries(npp_linux PRIVATE Qt6::Widgets)

set_property(TARGET npp_linux PROPERTY CXX_STANDARD 17)
set_property(TARGET npp_linux PROPERTY CXX_STANDARD_REQUIRED ON)
