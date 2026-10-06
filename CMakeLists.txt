cmake_minimum_required(VERSION 3.20)

project(FlowTools
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(flow_core SHARED
    core/src/core.cpp
    core/src/log.cpp
    core/src/terminal.cpp
)

target_include_directories(flow_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

target_compile_definitions(flow_core
    PRIVATE
        FLOW_CORE_BUILD
)

add_executable(fpath
    tools/fpath/main.cpp
)

target_compile_definitions(fpath
    PRIVATE
        FPATH_VERSION="0.1.0"
)

target_link_libraries(fpath
    PRIVATE
        flow_core
)

add_executable(fsearch
    tools/fsearch/main.cpp
)

target_compile_definitions(fsearch
    PRIVATE
        FSEARCH_VERSION="0.2.0"
)

target_link_libraries(fsearch
    PRIVATE
        flow_core
)

add_executable(fproc
    tools/fproc/main.cpp
)

target_compile_definitions(fproc
    PRIVATE
        FPROC_VERSION="0.1.0"
)

target_link_libraries(fproc
    PRIVATE
        flow_core
)

add_executable(fsize
    tools/fsize/main.cpp
)

target_compile_definitions(fsize
    PRIVATE
        FSIZE_VERSION="0.1.0"
)

target_link_libraries(fsize
    PRIVATE
        flow_core
)

add_executable(fenv
    tools/fenv/main.cpp
)

target_compile_definitions(fenv
    PRIVATE
        FENV_VERSION="0.1.0"
)

target_link_libraries(fenv
    PRIVATE
        flow_core
)

add_executable(fnet
    tools/fnet/main.cpp
)

target_compile_definitions(fnet
    PRIVATE
        FNET_VERSION="0.1.0"
)

target_link_libraries(fnet
    PRIVATE
        flow_core
)

add_executable(fport
    tools/fport/main.cpp
)

target_compile_definitions(fport
    PRIVATE
        FPORT_VERSION="0.1.0"
)

target_link_libraries(fport
    PRIVATE
        flow_core
)

add_executable(flow
    flow/src/main.cpp
)

target_compile_definitions(flow
    PRIVATE
        FLOW_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(flow
    PRIVATE
        flow_core
)