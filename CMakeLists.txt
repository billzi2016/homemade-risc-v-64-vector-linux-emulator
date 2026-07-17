# 文件职责：定义项目的顶层 C++17 构建、严格告警策略和测试入口。
# 边界：这里只负责目标编排，不包含任何模拟器硬件语义或宿主环境修改。

cmake_minimum_required(VERSION 3.20)

project(
    homemade_risc_v_64_vector_linux_emulator
    VERSION 0.1.0
    DESCRIPTION "A from-scratch educational RV64GCV full-system emulator"
    LANGUAGES CXX
)

option(RVEMU_WARNINGS_AS_ERRORS "将项目自身的编译告警视为错误" ON)

include(cmake/CompilerWarnings.cmake)

add_library(rvemu_bus_memory STATIC
    src/bus/address_range.cpp
    src/bus/bus.cpp
    src/memory/boot_rom.cpp
    src/memory/physical_memory.cpp
)

target_include_directories(rvemu_bus_memory
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(rvemu_bus_memory PUBLIC cxx_std_17)
rvemu_enable_strict_warnings(rvemu_bus_memory)

add_library(rvemu_core STATIC
    src/core/csr.cpp
    src/core/cpu.cpp
    src/core/cpu_state.cpp
    src/core/decoder.cpp
)

target_include_directories(rvemu_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(rvemu_core PUBLIC rvemu_bus_memory)
target_compile_features(rvemu_core PUBLIC cxx_std_17)
rvemu_enable_strict_warnings(rvemu_core)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
