cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    add_executable(prober_gui WIN32
        main.cpp
        MainWindow.h
        MainWindow.cpp
        Theme.h
        types.h
        backend/CliRunner.h
        backend/CliRunner.cpp
        models/EscListModel.h
        models/EscListModel.cpp
    )
else()
    add_executable(prober_gui
        main.cpp
        MainWindow.h
        MainWindow.cpp
        Theme.h
        types.h
        backend/CliRunner.h
        backend/CliRunner.cpp
        models/EscListModel.h
        models/EscListModel.cpp
    )
endif()

set_target_properties(prober_gui PROPERTIES OUTPUT_NAME "prober_gui_impl")

target_link_libraries(prober_gui PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

if(MINGW)
    target_link_options(prober_gui PRIVATE
        -static-libgcc -static-libstdc++
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
endif()

target_include_directories(prober_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
)

set_target_properties(prober_gui PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
