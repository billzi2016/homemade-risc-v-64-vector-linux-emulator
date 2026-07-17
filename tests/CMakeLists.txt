# 文件职责：构建并注册物理总线与内存模块的真实组件测试。
# 边界：测试目标只调用生产库，不编译 Mock、Stub 或另一套硬件实现。

add_executable(rvemu_bus_memory_tests
    unit/test_bus_memory.cpp
)

target_link_libraries(rvemu_bus_memory_tests PRIVATE rvemu_bus_memory)
target_compile_features(rvemu_bus_memory_tests PRIVATE cxx_std_17)
rvemu_enable_strict_warnings(rvemu_bus_memory_tests)

add_test(NAME rvemu.bus_memory COMMAND rvemu_bus_memory_tests)
