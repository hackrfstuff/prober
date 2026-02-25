cmake_minimum_required(VERSION 3.20)
project(esctool VERSION 1.1.3 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROBER_VERSION "${PROJECT_VERSION}")
set(PROBER_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(PROBER_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(PROBER_VERSION_PATCH ${PROJECT_VERSION_PATCH})
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/esctool/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/esctool/version.h
)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

option(ESCTOOL_WITH_DESKTOP_TRANSPORT "Build desktop serial transport" ON)

if(ESCTOOL_WITH_DESKTOP_TRANSPORT)
  include(FetchContent)
  FetchContent_Declare(
    CSerialPort
    GIT_REPOSITORY https://github.com/itas109/CSerialPort.git
    GIT_TAG        v4.3.3
    GIT_SHALLOW    TRUE
  )
  set(CSERIALPORT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CSERIALPORT_BUILD_TEST OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(CSerialPort)
  set(CSERIALPORT_INCLUDE_DIR ${cserialport_SOURCE_DIR}/include)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(esctool_core STATIC
  src/Log.cpp
  src/Utils.cpp
  src/MSP.cpp
  src/FourWay.cpp
  src/Bluejay.cpp
  src/BluejaySettings.cpp
  src/IntelHex.cpp
  src/Silabs.cpp
  src/Flasher.cpp
  src/C2Flasher.cpp
)

if(ESCTOOL_WITH_DESKTOP_TRANSPORT)
  target_sources(esctool_core PRIVATE src/transport/SerialCSerialPort.cpp)
  target_include_directories(esctool_core PRIVATE src/transport ${CSERIALPORT_INCLUDE_DIR})
  target_link_libraries(esctool_core PRIVATE libcserialport)
endif()
add_executable(prober_cli src/flasher_main.cpp)
set_target_properties(prober_cli PROPERTIES OUTPUT_NAME "prober")
target_include_directories(prober_cli PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/transport
  ${CSERIALPORT_INCLUDE_DIR}
)
target_link_libraries(prober_cli PRIVATE libcserialport)

if(MSVC)
  target_compile_options(esctool_core PRIVATE /W4)
  target_compile_options(prober_cli PRIVATE /W4)
else()
  target_compile_options(esctool_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(prober_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(prober_cli PRIVATE esctool_core)

if(WIN32)
  target_link_libraries(esctool_core PRIVATE setupapi)
endif()

if(MINGW)
  target_link_options(prober_cli PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
endif()

option(BUILD_GUI "Build native Qt GUI" ON)
if(BUILD_GUI)
  add_subdirectory(gui)
  add_subdirectory(gui_launcher)
endif()
