add_executable(prober_gui_launcher WIN32 main.cpp)
set_target_properties(prober_gui_launcher PROPERTIES
    OUTPUT_NAME "prober_gui"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gui_launcher"
)
