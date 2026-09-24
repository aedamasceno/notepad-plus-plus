cmake_minimum_required(VERSION 3.24)

find_package(Qt6 COMPONENTS Widgets Core5Compat PrintSupport Concurrent REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SCINTILLA_EDIT_DIR "${REPOSITORY_ROOT}/scintilla/qt/ScintillaEdit")
set(SCINTILLAEDIT_H "${SCINTILLA_EDIT_DIR}/ScintillaEdit.h")
set(SCINTILLAEDIT_CPP "${SCINTILLA_EDIT_DIR}/ScintillaEdit.cpp")

add_custom_command(
    OUTPUT "${SCINTILLAEDIT_H}" "${SCINTILLAEDIT_CPP}"
    COMMAND "${Python3_EXECUTABLE}" "${SCINTILLA_EDIT_DIR}/WidgetGen.py"
    WORKING_DIRECTORY "${SCINTILLA_EDIT_DIR}"
    DEPENDS
        "${REPOSITORY_ROOT}/scintilla/include/Scintilla.iface"
        "${SCINTILLA_EDIT_DIR}/ScintillaEdit.cpp.template"
        "${SCINTILLA_EDIT_DIR}/ScintillaEdit.h.template"
        "${SCINTILLA_EDIT_DIR}/WidgetGen.py"
    COMMENT "Generating Scintilla Qt wrapper"
    VERBATIM
)
set_source_files_properties("${SCINTILLAEDIT_CPP}" "${SCINTILLAEDIT_H}"
    PROPERTIES GENERATED TRUE)

file(GLOB_RECURSE SCINTILLA_CORE_SOURCES CONFIGURE_DEPENDS
    "${REPOSITORY_ROOT}/scintilla/src/*.c"
    "${REPOSITORY_ROOT}/scintilla/src/*.cpp"
    "${REPOSITORY_ROOT}/scintilla/src/*.cxx")
file(GLOB_RECURSE QT_EDIT_BASE_SOURCES CONFIGURE_DEPENDS
    "${REPOSITORY_ROOT}/scintilla/qt/ScintillaEditBase/*.cpp"
    "${REPOSITORY_ROOT}/scintilla/qt/ScintillaEditBase/*.cxx")
file(GLOB_RECURSE QT_EDIT_BASE_HEADERS CONFIGURE_DEPENDS
    "${REPOSITORY_ROOT}/scintilla/qt/ScintillaEditBase/*.h")
set(QT_EDIT_SOURCES "${SCINTILLA_EDIT_DIR}/ScintillaDocument.cpp")
set(QT_EDIT_HEADERS "${SCINTILLA_EDIT_DIR}/ScintillaDocument.h")

file(GLOB LEXILLA_CORE_SOURCES CONFIGURE_DEPENDS "${REPOSITORY_ROOT}/lexilla/src/*.cxx")
file(GLOB LEXILLA_LEXLIB_SOURCES CONFIGURE_DEPENDS "${REPOSITORY_ROOT}/lexilla/lexlib/*.cxx")
file(GLOB LEXILLA_LEXER_SOURCES CONFIGURE_DEPENDS "${REPOSITORY_ROOT}/lexilla/lexers/*.cxx")
list(FILTER LEXILLA_LEXER_SOURCES EXCLUDE REGEX ".*/LexUser\\.cxx$")

add_library(textpinnacle_support STATIC
    findreplace.cpp
    settingsmigration.cpp
    searchmanager.cpp
    sessionmanager.cpp
    functionlist.cpp
    filebrowser.cpp
    documentlist.cpp
    documentmap.cpp
    editorutils.cpp
    documentformat.cpp
    printhelper.cpp
    macromanager.cpp
    dualviewmanager.cpp
    ${SCINTILLA_CORE_SOURCES}
    ${QT_EDIT_BASE_SOURCES}
    ${QT_EDIT_BASE_HEADERS}
    ${QT_EDIT_SOURCES}
    ${QT_EDIT_HEADERS}
    ${LEXILLA_CORE_SOURCES}
    ${LEXILLA_LEXLIB_SOURCES}
    ${LEXILLA_LEXER_SOURCES}
    "${SCINTILLAEDIT_CPP}"
    "${SCINTILLAEDIT_H}")

target_include_directories(textpinnacle_support PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${REPOSITORY_ROOT}/lexilla/include"
    "${REPOSITORY_ROOT}/lexilla/lexlib"
    "${REPOSITORY_ROOT}/lexilla/src"
    "${REPOSITORY_ROOT}/scintilla/include"
    "${REPOSITORY_ROOT}/scintilla/src"
    "${REPOSITORY_ROOT}/scintilla/qt/ScintillaEditBase"
    "${SCINTILLA_EDIT_DIR}")
target_compile_definitions(textpinnacle_support PUBLIC
    SCINTILLA_QT MAKING_LIBRARY=1 LEXILLA_STATIC=1 LINUX_BUILD=1)
target_compile_options(textpinnacle_support PRIVATE -include cstdint)
target_link_libraries(textpinnacle_support PUBLIC Qt6::Widgets Qt6::Core5Compat Qt6::PrintSupport Qt6::Concurrent)
target_compile_features(textpinnacle_support PUBLIC cxx_std_17)

add_library(textpinnacle_app STATIC mainwindow.cpp)
target_link_libraries(textpinnacle_app PUBLIC textpinnacle_support)

add_executable(textpinnacle main.cpp resources.qrc)
target_link_libraries(textpinnacle PRIVATE textpinnacle_app)

if(BUILD_TESTING)
    add_executable(linux_search_tests tests/searchtests.cpp)
    target_link_libraries(linux_search_tests PRIVATE textpinnacle_app)
    add_test(NAME linux_search_tests COMMAND linux_search_tests)
    set_tests_properties(linux_search_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(linux_encoding_eol_tests tests/encodingeoltests.cpp)
    target_link_libraries(linux_encoding_eol_tests PRIVATE textpinnacle_app)
    add_test(NAME linux_encoding_eol_tests COMMAND linux_encoding_eol_tests)
    set_tests_properties(linux_encoding_eol_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(linux_dualview_tests tests/dualviewtests.cpp)
    target_link_libraries(linux_dualview_tests PRIVATE textpinnacle_app)
    add_test(NAME linux_dualview_tests COMMAND linux_dualview_tests)
    set_tests_properties(linux_dualview_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(linux_macro_tests tests/macrotests.cpp)
    target_link_libraries(linux_macro_tests PRIVATE textpinnacle_app)
    add_test(NAME linux_macro_tests COMMAND linux_macro_tests)
    set_tests_properties(linux_macro_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(linux_printing_tests tests/printingtests.cpp)
    target_link_libraries(linux_printing_tests PRIVATE textpinnacle_support)
    add_test(NAME linux_printing_tests COMMAND linux_printing_tests)
    set_tests_properties(linux_printing_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(linux_panel_tests tests/paneltests.cpp)
    target_link_libraries(linux_panel_tests PRIVATE textpinnacle_app)
    add_test(NAME linux_panel_tests COMMAND linux_panel_tests)
    set_tests_properties(linux_panel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(linux_recovery_tests tests/recoverytests.cpp)
    target_link_libraries(linux_recovery_tests PRIVATE textpinnacle_app)
    add_test(NAME linux_recovery_tests COMMAND linux_recovery_tests)
    set_tests_properties(linux_recovery_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(linux_settings_migration_tests tests/settingsmigrationtests.cpp)
    target_link_libraries(linux_settings_migration_tests PRIVATE textpinnacle_support)
    add_test(NAME linux_settings_migration_tests COMMAND linux_settings_migration_tests)
endif()

include(GNUInstallDirs)
install(TARGETS textpinnacle RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${REPOSITORY_ROOT}/packaging/textpinnacle.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES "${REPOSITORY_ROOT}/packaging/io.github.aedamasceno.textpinnacle.metainfo.xml"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
set(TEXTPINNACLE_ICON "${REPOSITORY_ROOT}/packaging/textpinnacle.png")
if(EXISTS "${TEXTPINNACLE_ICON}")
    install(FILES "${TEXTPINNACLE_ICON}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
else()
    message(STATUS
        "TextPinnacle product icon not installed: add packaging/textpinnacle.png")
endif()
