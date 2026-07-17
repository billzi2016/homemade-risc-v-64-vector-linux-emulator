# 文件职责：构建并注册物理总线与内存模块的真实组件测试。
# 边界：测试目标只调用生产库，不编译 Mock、Stub 或另一套硬件实现。

add_executable(rvemu_bus_memory_tests
    unit/test_bus_memory.cpp
)

target_link_libraries(rvemu_bus_memory_tests PRIVATE rvemu_bus_memory)
target_compile_features(rvemu_bus_memory_tests PRIVATE cxx_std_17)
rvemu_enable_strict_warnings(rvemu_bus_memory_tests)

add_test(NAME rvemu.bus_memory COMMAND rvemu_bus_memory_tests)

add_executable(rvemu_cpu_rv64i_tests
    unit/test_cpu_rv64i.cpp
)

target_link_libraries(rvemu_cpu_rv64i_tests PRIVATE rvemu_core)
target_compile_features(rvemu_cpu_rv64i_tests PRIVATE cxx_std_17)
rvemu_enable_strict_warnings(rvemu_cpu_rv64i_tests)

add_test(NAME rvemu.cpu_rv64i COMMAND rvemu_cpu_rv64i_tests)

add_executable(rvemu_cpu_privilege_tests
    unit/test_cpu_privilege.cpp
)

target_link_libraries(rvemu_cpu_privilege_tests PRIVATE rvemu_core)
target_compile_features(rvemu_cpu_privilege_tests PRIVATE cxx_std_17)
rvemu_enable_strict_warnings(rvemu_cpu_privilege_tests)

add_test(NAME rvemu.cpu_privilege COMMAND rvemu_cpu_privilege_tests)
