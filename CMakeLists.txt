cmake_minimum_required(VERSION 3.16)

project(BandPilot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(BandPilot
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(BandPilot ${PROJECT_SOURCES})
endif()

target_link_libraries(BandPilot PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

target_include_directories(BandPilot PRIVATE src)

target_compile_options(BandPilot PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

set_target_properties(BandPilot PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME BandPilot
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

include(GNUInstallDirs)
install(TARGETS BandPilot
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(BandPilot)
endif()
