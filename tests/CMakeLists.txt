qt_add_executable(tst_cty
    tst_cty.cpp
    ../src/cty.h
    ../src/cty.cpp
)

target_include_directories(tst_cty PRIVATE ../src)

target_link_libraries(tst_cty PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME tst_cty COMMAND tst_cty)

# Deploy the Qt runtime DLLs (notably Qt6Test, which BandPilot's own
# windeployqt step doesn't cover) next to the test executable so it can run
# standalone, e.g. via ctest, without relying on another target's DLLs
# already being present in the build directory.
if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET tst_cty POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:tst_cty>"
        COMMENT "Running windeployqt for tst_cty")
endif()
