cmake_minimum_required(VERSION 3.19)

project(BandPilot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

qt_standard_project_setup()

qt_add_executable(BandPilot
    src/main.cpp
    src/version.h
    src/mainwindow.h
    src/mainwindow.cpp
    src/database.h
    src/database.cpp
    src/dxcc_entities.h
)

target_include_directories(BandPilot PRIVATE src)

target_link_libraries(BandPilot PRIVATE
    Qt6::Widgets
    Qt6::Sql
)

set_target_properties(BandPilot PROPERTIES
    WIN32_EXECUTABLE ON
)

# Copy the Qt runtime DLLs next to the executable on Windows so it can be
# run/debugged straight from the build directory.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET BandPilot POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:BandPilot>"
            COMMENT "Running windeployqt")
    endif()
endif()
