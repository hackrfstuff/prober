cmake_minimum_required(VERSION 3.5)
project(serial LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

set(serial_SRCS
    src/serial.cc
    include/serial/serial.h
    include/serial/v8stdint.h
)

if(APPLE)
    list(APPEND serial_SRCS
        src/impl/unix.cc
        src/impl/list_ports/list_ports_osx.cc
    )
    find_library(IOKIT_LIBRARY IOKit)
    find_library(FOUNDATION_LIBRARY Foundation)
elseif(UNIX)
    list(APPEND serial_SRCS
        src/impl/unix.cc
        src/impl/list_ports/list_ports_linux.cc
    )
else() # Windows / MinGW
    list(APPEND serial_SRCS
        src/impl/win.cc
        src/impl/list_ports/list_ports_win.cc
    )
endif()

add_library(${PROJECT_NAME} ${serial_SRCS})
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FOUNDATION_LIBRARY} ${IOKIT_LIBRARY})
elseif(UNIX)
    find_package(Threads REQUIRED)
    # 'rt' is needed on some Linuxes; harmless where absent
    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads rt)
else() # Windows / MinGW
    target_link_libraries(${PROJECT_NAME} PRIVATE setupapi)
endif()

add_executable(serial_example examples/serial_example.cc)
target_link_libraries(serial_example PRIVATE ${PROJECT_NAME})

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/serial/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/serial)
