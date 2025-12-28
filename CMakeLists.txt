cmake_minimum_required(VERSION 3.16)

project(CheatPad VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick DBus Sql)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appCheatPad
    main.cpp
    AppController.h
)

qt_add_qml_module(appCheatPad
    URI CheatPad
    VERSION 1.0
    QML_FILES
        Main.qml
    SOURCES
        SheetModel.h
        SheetModel.cpp
        AppController.cpp
        CsvHandler.h
        CsvHandler.cpp
)

set_target_properties(appCheatPad PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appCheatPad
    PRIVATE Qt6::Quick
    Qt6::DBus
    Qt6::Sql
)

include(GNUInstallDirs)
install(TARGETS appCheatPad
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
