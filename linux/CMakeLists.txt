cmake_minimum_required(VERSION 3.24)
project(npp_linux LANGUAGES CXX)

find_package(Qt6 COMPONENTS Widgets Core5Compat PrintSupport REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Enable Qt meta‑object compilation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Generate ScintillaEdit Qt wrapper from upstream templates
set(SCINTILLA_EDIT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEdit"
)
set(SCINTILLAEDIT_H
    "${SCINTILLA_EDIT_DIR}/ScintillaEdit.h"
)
set(SCINTILLAEDIT_CPP
    "${SCINTILLA_EDIT_DIR}/ScintillaEdit.cpp"
)

add_custom_command(
    OUTPUT
        "${SCINTILLAEDIT_H}"
        "${SCINTILLAEDIT_CPP}"
    COMMAND
        "${Python3_EXECUTABLE}" "${SCINTILLA_EDIT_DIR}/WidgetGen.py"
    WORKING_DIRECTORY
        "${SCINTILLA_EDIT_DIR}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/include/Scintilla.iface"
        "${SCINTILLA_EDIT_DIR}/ScintillaEdit.cpp.template"
        "${SCINTILLA_EDIT_DIR}/ScintillaEdit.h.template"
        "${SCINTILLA_EDIT_DIR}/WidgetGen.py"
    COMMENT "Generating ScintillaEdit.h and ScintillaEdit.cpp"
    VERBATIM
)

# ------------------------------------------------------------------
# Gather all Scintilla core sources (C and C++)
file(GLOB_RECURSE SCINTILLA_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/src/*.cxx"
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

list(REMOVE_ITEM QT_EDIT_SOURCES "${SCINTILLAEDIT_CPP}")
list(REMOVE_ITEM QT_EDIT_HEADERS "${SCINTILLAEDIT_H}")

set_source_files_properties(
    "${SCINTILLAEDIT_CPP}"
    "${SCINTILLAEDIT_H}"
    PROPERTIES GENERATED TRUE
)

# ------------------------------------------------------------------
# Lexilla
file(GLOB LEXILLA_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../lexilla/src/*.cxx"
)

file(GLOB LEXILLA_LEXLIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../lexilla/lexlib/*.cxx"
)

file(GLOB LEXILLA_LEXER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../lexilla/lexers/*.cxx"
)

# Notepad++ User Defined Language lexer is Windows-specific.
list(FILTER LEXILLA_LEXER_SOURCES EXCLUDE REGEX ".*/LexUser\\.cxx$")

# ------------------------------------------------------------------
add_executable(npp_linux
    main.cpp
    findreplace.cpp
    sessionmanager.cpp
    functionlist.cpp
    functionlist.h
    filebrowser.cpp
    filebrowser.h
    documentlist.cpp
    documentlist.h
    resources.qrc

    ${LEXILLA_CORE_SOURCES}
    ${LEXILLA_LEXLIB_SOURCES}
    ${LEXILLA_LEXER_SOURCES}
    ${SCINTILLA_CORE_SOURCES}
    ${QT_EDIT_BASE_SOURCES}
    ${QT_EDIT_BASE_HEADERS}
    ${QT_EDIT_SOURCES}
    ${QT_EDIT_HEADERS}
    "${SCINTILLAEDIT_CPP}"
    "${SCINTILLAEDIT_H}"
)

target_include_directories(npp_linux PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../lexilla/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lexilla/lexlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lexilla/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEditBase"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scintilla/qt/ScintillaEdit"
)

# Define the macro that enables Qt specific code in Scintilla
target_compile_definitions(npp_linux PRIVATE SCINTILLA_QT)

target_link_libraries(npp_linux PRIVATE Qt6::Widgets Qt6::Core5Compat Qt6::PrintSupport)

set_property(TARGET npp_linux PROPERTY CXX_STANDARD 17)
set_property(TARGET npp_linux PROPERTY CXX_STANDARD_REQUIRED ON)

# Qt printing support
target_link_libraries(npp_linux PRIVATE Qt6::PrintSupport)
