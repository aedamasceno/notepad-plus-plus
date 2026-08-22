cmake_minimum_required(VERSION 3.24)
project(npp_linux LANGUAGES CXX)

find_package(Qt6 COMPONENTS Widgets Core5Compat REQUIRED)

# Enable Qt meta‑object compilation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ------------------------------------------------------------------
# Gather all Scintilla core sources (C and C++)
file(GLOB_RECURSE SCINTILLA_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/src/*.cpp"
)

# Gather the Qt wrapper sources for the widget base
file(GLOB_RECURSE QT_EDIT_BASE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/qt/ScintillaEditBase/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/qt/ScintillaEditBase/*.cxx"
)
file(GLOB_RECURSE QT_EDIT_BASE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/qt/ScintillaEditBase/*.h"
)

# Gather the Qt wrapper sources for the widget itself
file(GLOB_RECURSE QT_EDIT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/qt/ScintillaEdit/*.cpp"
)
file(GLOB_RECURSE QT_EDIT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/qt/ScintillaEdit/*.h"
)

# ------------------------------------------------------------------
# Gather Lexilla sources
file(GLOB_RECURSE LEXILLA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/lexilla/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lexilla/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/lexilla/src/*.cpp"
)

add_executable(npp_linux linux/main.cpp linux/findreplace.cpp linux/sessionmanager.cpp linux/resources.qrc
    ${SCINTILLA_CORE_SOURCES}
    ${QT_EDIT_BASE_SOURCES}
    ${QT_EDIT_BASE_HEADERS}
    ${QT_EDIT_SOURCES}
    ${QT_EDIT_HEADERS}
    ${LEXILLA_SOURCES}
)

target_include_directories(npp_linux PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/qt/ScintillaEditBase"
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/qt/ScintillaEdit"
    "${CMAKE_CURRENT_SOURCE_DIR}/scintilla/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lexilla/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/lexilla/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lexilla/lexlib"
)

# Add compile option to include cstdint for all sources to fix intptr_t undefined error
target_compile_options(npp_linux PRIVATE -include cstdint)

# Define the macro that enables Qt specific code in Scintilla
target_compile_definitions(npp_linux PRIVATE SCINTILLA_QT MAKING_LIBRARY=1 LEXILLA_STATIC=1)

target_link_libraries(npp_linux PRIVATE Qt6::Widgets Qt6::Core5Compat)

set_property(TARGET npp_linux PROPERTY CXX_STANDARD 17)
set_property(TARGET npp_linux PROPERTY CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

install(TARGETS npp_linux
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES packaging/notepad-plus-plus.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES packaging/io.github.aedamasceno.notepad-plus-plus.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

install(FILES PowerEditor/misc/chameleon/chameleon-pencil-200.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME notepad-plus-plus.png
)
