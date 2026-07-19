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
    src/core/compressed_decoder.cpp
    src/core/csr.cpp
    src/core/cpu.cpp
    src/core/cpu_floating.cpp
    src/core/cpu_state.cpp
    src/core/decoder.cpp
    src/core/floating_state.cpp
    src/core/integer_a.cpp
    src/core/integer_m.cpp
    src/core/soft_float_arithmetic.cpp
    src/core/soft_float_conversion.cpp
    src/memory/mmu.cpp
    src/vector/vector_state.cpp
)

target_include_directories(rvemu_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(rvemu_core PUBLIC rvemu_bus_memory)
target_compile_features(rvemu_core PUBLIC cxx_std_17)
rvemu_enable_strict_warnings(rvemu_core)

add_library(rvemu_conformance STATIC
    src/conformance/elf_loader.cpp
)

target_include_directories(rvemu_conformance
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(rvemu_conformance PUBLIC rvemu_bus_memory)
target_compile_features(rvemu_conformance PUBLIC cxx_std_17)
rvemu_enable_strict_warnings(rvemu_conformance)

add_executable(rvemu_conformance_runner
    src/conformance/main.cpp
)

target_link_libraries(rvemu_conformance_runner
    PRIVATE
        rvemu_conformance
        rvemu_core
)
target_compile_features(rvemu_conformance_runner PRIVATE cxx_std_17)
rvemu_enable_strict_warnings(rvemu_conformance_runner)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
