if(WIN32)
    add_executable(prober_gui_launcher WIN32 main.cpp)
else()
    add_executable(prober_gui_launcher main.cpp)
endif()
set_target_properties(prober_gui_launcher PROPERTIES
    OUTPUT_NAME "prober_gui"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gui_launcher"
)
if(MINGW)
    target_link_options(prober_gui_launcher PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
endif()
