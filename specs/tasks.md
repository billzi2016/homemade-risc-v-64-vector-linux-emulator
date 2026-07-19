# 项目任务清单

## 1. 使用规则

- `- [ ]` 表示尚未满足全部完成条件；`- [x]` 表示需求、实现、验证和文档均已完成。
- 任务必须按依赖顺序执行，前置任务未完成时不得勾选下游任务。
- 每次勾选必须附有可复核证据，例如测试命令、日志路径、规范测试结果或人工验收记录。
- Mock、Stub、占位实现、硬编码输出、仅编译通过和未覆盖真实路径的快速验证均不构成完成证据。
- 任务状态必须以条目下的实际证据为准；未列出通过证据的任务保持未勾选。

## 2. 阶段 0：治理与规格基线

- [ ] **SDD-001** 审阅并确认 `AGENTS.md` 与 `constitution.md`。
  - 完成条件：用户确认规则完整，无未解决冲突。
- [ ] **SDD-002** 审阅所有专题规格及需求编号。
  - 完成条件：CPU、MMU、总线、设备、运行时、测试范围均获确认。
- [ ] **SDD-003** 建立需求追踪矩阵。
  - 完成条件：每个强制需求都映射到实现任务和验证方法。
- [ ] **SDD-004** 冻结第一版机器模型和适用标准版本。
  - 完成条件：RISC-V 特权/非特权、RVV、VirtIO MMIO 和设备版本均明确。

## 3. 阶段 1：工程骨架与基础设施

- [x] **BLD-001** 建立 C++17+ 构建系统和严格编译告警配置。
  - 实现文件：`CMakeLists.txt`、`cmake/CompilerWarnings.cmake`
  - 验证命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`；`cmake --build build --parallel`
  - 验证结果：AppleClang 21 使用 C++17 和 `-Werror` 完成生产库及测试目标编译，0 告警、0 错误。
  - 已知限制：当前只建立首个生产模块目标，后续模块必须复用同一告警函数。
- [ ] **BLD-002** 按 `project-tree.md` 建立模块目录和最小接口边界。
- [ ] **BLD-003** 建立统一错误、诊断和类型安全的位操作基础。
- [x] **BLD-004** 建立测试目录、正式测试框架和可复现测试入口。
  - 实现文件：`tests/CMakeLists.txt`、`tests/unit/test_bus_memory.cpp`
  - 验证命令：`ctest --test-dir build --output-on-failure`
  - 验证结果：CTest 运行真实 Bus/RAM/Boot ROM 生产组件，1/1 通过、0 失败。
  - 日志或报告：`build/Testing/Temporary/LastTest.log`
  - 已知限制：后续模块需要继续增加 integration、conformance 与 system 测试目标。
- [ ] **BLD-005** 建立外部产物目录及精确 `.gitignore` 规则。
  - 完成条件：大文件、镜像、日志和缓存不会被 Git 跟踪，源码及校验清单不被误排除。

## 4. 阶段 2：物理内存与总线

- [x] **BUS-001** 实现统一的 8/16/32/64 位物理总线访问接口。（`BUS-REQ-*`）
  - 实现文件：`include/rvemu/bus/`、`src/bus/`
  - 验证命令：`cmake --build build --parallel`；`ctest --test-dir build --output-on-failure`
  - 验证结果：四种宽度的真实读写、小端序、跨界、未映射、地址溢出和 compare-exchange 均通过。
  - 对应需求：`BUS-REQ-001`、`BUS-REQ-002`、`BUS-REQ-003`
- [x] **BUS-002** 实现 RAM 边界检查、小端序读写和可配置容量。（`BUS-REQ-*`）
  - 实现文件：`include/rvemu/memory/physical_memory.hpp`、`src/memory/physical_memory.cpp`
  - 验证结果：非对齐小端访问、各宽度高位截断、末边界无部分写入和窄宽度原子事务测试通过。
  - 日志或报告：`build/Testing/Temporary/LastTest.log`
  - 对应需求：`BUS-REQ-001`、`BUS-REQ-002`
- [x] **BUS-003** 实现只读 Boot ROM 及初始化期受控装载。（`BUS-REQ-*`）
  - 实现文件：`include/rvemu/memory/boot_rom.hpp`、`src/memory/boot_rom.cpp`
  - 验证结果：密封前装载、装载越界、密封后拒绝装载、运行期写入和原子写拒绝均通过。
  - 日志或报告：`build/Testing/Temporary/LastTest.log`
  - 对应需求：`BUS-REQ-001`、`BUS-REQ-003`
- [ ] **BUS-004** 实现 MMIO 区间注册、重叠检测和访问错误。（`BUS-REQ-*`）
- [ ] **BUS-005** 验证全部固定地址区间和越界行为。

## 5. 阶段 3：CPU 基础状态与 RV64I

- [x] **CPU-001** 实现标量、浮点、向量寄存器状态和 x0 写保护。（`CPU-REQ-*`）
  - 实现文件：`include/rvemu/core/cpu_state.hpp`、`src/core/cpu_state.cpp`
  - 验证结果：32 个整数、32 个浮点和 32×256 位向量寄存器状态测试通过；整数唯一写入口拒绝修改 x0。
  - 对应需求：`CPU-REQ-001`、`CPU-REQ-002`、`CPU-REQ-003`、`CPU-REQ-004`、`CPU-REQ-005`
- [x] **CPU-002** 实现统一取指，正确区分 16 位与 32 位指令。（`ISA-REQ-*`）
  - 实现文件：`include/rvemu/core/cpu.hpp`、`src/core/cpu.cpp`
  - 验证结果：2 字节/4 字节长度识别、奇地址、未映射地址和跨 RAM 边界第二半字取指错误均通过精确异常测试。
  - 对应需求：`ISA-REQ-001`、`ISA-REQ-006`、`TRAP-REQ-001`、`TRAP-REQ-002`
- [x] **CPU-003** 完整实现 RV64I 算术、逻辑、分支、跳转和访存。
  - 实现文件：`src/core/cpu.cpp`、`src/core/decoder.cpp`
  - 验证结果：RV64I 各指令族、正负立即数、寄存器别名、回绕、加载扩展和存储宽度均通过真实机器码测试。
- [x] **CPU-004** 实现 RV64I 系统指令和非法指令异常。
  - 实现文件：`src/core/cpu.cpp`、`include/rvemu/core/trap.hpp`
  - 验证结果：`FENCE`、`FENCE.I`、`ECALL`、`EBREAK`、未定义 opcode 和保留编码的精确异常测试通过。
- [x] **CPU-005** 通过边界值、对齐、溢出和 PC 更新测试。
  - 验证命令：`ctest --test-dir build --output-on-failure`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：普通严格构建和 ASan/UBSan 构建中的 `rvemu.cpu_rv64i` 均通过；失败访存不提交目标寄存器或 PC。

## 6. 阶段 4：CSR、特权态与陷阱

- [x] **PRV-001** 实现 M/S/U 三种特权模式和合法转换。（`CPU-REQ-*`）
  - 实现文件：`include/rvemu/core/csr.hpp`、`src/core/csr.cpp`、`src/core/cpu.cpp`
  - 验证结果：复位进入 M-mode，M/S/U Trap 与 `MRET/SRET` 往返、非法降权返回和状态栈恢复测试通过。
- [x] **PRV-002** 实现 CSR 地址权限、只读属性和原子读改写语义。
  - 实现文件：`include/rvemu/core/csr.hpp`、`src/core/csr.cpp`
  - 验证结果：六种 Zicsr 真实机器码、最低权限、只读编码、条件写、WARL、TVM 和 counteren 门控测试通过。
- [x] **PRV-003** 实现 `mstatus/sstatus`、`mie/sie`、`mip/sip` 的别名与受限视图。
  - 验证结果：S 级视图直接投影唯一 M 级状态；别名写入保留 M-only 位，设备 pending 位不被软件错误清除。
- [ ] **PRV-004** 完成 `medeleg/mideleg` 及 M/S 陷阱委托父级里程碑。
  - [x] **PRV-004A** 实现 `medeleg/mideleg` 的存在位、只读零位和 WARL 委托掩码。
    - 验证结果：全一写入后只读回可委托同步异常位和 SSIP/STIP/SEIP 位；M-mode ECALL 等不可委托位保持零。
  - [x] **PRV-004B** 验证 `mie/mip` 与 `sie/sip` 委托受限视图不会形成两套状态。
    - 验证结果：`sie/sip` 读取受 `mideleg` 限制，写入直接修改共享 `mie/mip` 的允许位，无独立 S 状态副本。
  - [x] **PRV-004C** 实现每一种可委托同步异常的目标特权级选择。
    - 验证结果：真实 CPU Trap 入口穷举 13 种可委托同步异常及不可委托 M-mode ECALL 边界，并覆盖 U/S/M 来源与 M/S 目标选择。
  - [x] **PRV-004D** 实现软件、定时器和外部中断的委托目标选择。
    - 验证结果：真实 CSR 中断选择器和 CPU 注入入口覆盖 MSIP/MTIP/MEIP、SSIP/STIP/SEIP 的 pending、enable、委托目标与 Trap 提交。
  - [x] **PRV-004E** 实现不同当前特权级下的全局中断使能与抢占规则。
    - 验证结果：覆盖 M/S/U 当前模式；验证 MIE/SIE 关闭时同级中断被屏蔽、较高目标级抢占，以及已委托 S 中断在 M-mode 被屏蔽。
  - [x] **PRV-004F** 验证委托前后 `epc/cause/tval/status` 只写入正确目标 CSR。
    - 验证结果：同步异常与六种中断均验证 M/S Trap CSR 隔离、来源 PC、cause、tval=0（中断）及目标层状态栈更新。
  - [ ] **PRV-004G** 使用真实 OpenSBI 验证向 S-mode Linux 的委托链路。
  - 验证命令：`cmake --build build --parallel`；`./build/tests/rvemu_cpu_privilege_tests`；`ctest --test-dir build --output-on-failure`；`cmake --build build/sanitize --parallel`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：严格构建通过；特权专项测试通过；常规与 ASan/UBSan CTest 均为 11/11 通过。
  - 完成条件：以上子任务全部完成，真实 OpenSBI/Linux 不在中断初始化阶段卡死，且有 Trap 状态证据。
- [x] **PRV-005** 实现 `ECALL`、`EBREAK`、`MRET`、`SRET`、`WFI` 与陷阱入口。
  - 实现文件：`src/core/cpu.cpp`、`src/core/csr.cpp`
  - 验证结果：真实 SYSTEM 机器码、TSR/TW 拦截、WFI 停顿/唤醒、M/S Trap 状态写入和 xRET 恢复测试通过。
- [x] **PRV-006** 验证直接/向量化 `tvec`、中断优先级和返回状态恢复。
  - 验证命令：`ctest --test-dir build --output-on-failure`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：MEI/MSI/MTI/SEI/SSI/STI 固定优先级、Direct/Vectored 入口、interrupt cause 最高位和 M/S 返回栈均通过；完整测试 3/3 通过。

## 7. 阶段 5：M、A、F、D、C 扩展

- [x] **ISA-101** 完整实现 M 扩展及除零、溢出语义。
  - 实现文件：`include/rvemu/core/integer_m.hpp`、`src/core/integer_m.cpp`、`src/core/cpu.cpp`、`src/core/csr.cpp`
  - 验证命令：`ctest --test-dir build --output-on-failure`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：全部 13 条 RV64M/M-W 指令、乘法高半部、除零、最小负数除以负一、`.W` 符号扩展、寄存器别名、x0 和保留编码测试通过；普通与 ASan/UBSan 完整测试均为 4/4 通过。
- [x] **ISA-102** 实现 LR/SC 保留集、失效条件和 `.W/.D` 语义。
  - 实现文件：`include/rvemu/bus/access.hpp`、`include/rvemu/bus/bus.hpp`、`src/bus/bus.cpp`、`include/rvemu/core/cpu_state.hpp`、`src/core/cpu_state.cpp`、`src/core/cpu.cpp`
  - 验证命令：`ctest --test-dir build --output-on-failure`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：LR/SC `.W/.D`、自然对齐、一次性 token、精确范围、普通写/DMA/AMO 重叠失效、不重叠保持和失败无副作用测试通过；普通与 ASan/UBSan 完整测试均为 5/5 通过。
  - 对应需求：`ISA-REQ-003`、`BUS-REQ-008`、`BUS-REQ-009`、`BUS-REQ-010`、`BUS-REQ-011`
- [x] **ISA-103** 实现规范要求的 AMO 运算与内存原子性。
  - 实现文件：`include/rvemu/core/integer_a.hpp`、`src/core/integer_a.cpp`、`src/core/cpu.cpp`、`src/core/csr.cpp`、`src/bus/bus.cpp`
  - 验证命令：`ctest --test-dir build --output-on-failure`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：AMOSWAP/ADD/XOR/AND/OR/MIN/MAX/MINU/MAXU 的 `.W/.D` 全部 18 种形式、旧值写回、32 位符号扩展、回绕、aq/rl、别名、x0、保留编码和原子提交测试通过；普通与 ASan/UBSan 完整测试均为 5/5 通过。
  - 对应需求：`ISA-REQ-003`、`BUS-REQ-003`、`BUS-REQ-010`
- [x] **ISA-104** 实现浮点状态、舍入模式、异常标志和 NaN boxing。
  - 实现文件：`include/rvemu/core/floating_state.hpp`、`src/core/floating_state.cpp`、`include/rvemu/core/csr.hpp`、`src/core/csr.cpp`、`include/rvemu/core/cpu_state.hpp`、`src/core/cpu_state.cpp`
  - 验证命令：`ctest --test-dir build --output-on-failure`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：`fflags/frm/fcsr` 别名、FS=Off 门控、FS Dirty/SD 派生、五类异常标志累积、静态/动态舍入解析、保留编码拒绝及合法/非法 NaN box 测试通过；普通与 ASan/UBSan 完整测试均为 6/6 通过。
  - 对应需求：`CPU-REQ-002`、`CPU-REQ-016`、`CPU-REQ-017`、`CPU-REQ-018`、`CPU-REQ-019`、`ISA-REQ-004`
- [x] **ISA-105** 完整实现声明范围内的 F/D 指令。
  - 实现文件：`include/rvemu/core/soft_float.hpp`、`src/core/soft_float_internal.hpp`、`src/core/soft_float_arithmetic.cpp`、`src/core/soft_float_conversion.cpp`、`src/core/cpu_floating.cpp`、`src/core/cpu.cpp`、`src/core/csr.cpp`
  - 验证命令：`ctest --test-dir build --output-on-failure`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：F/D 2.2 加载存储、单双精度四种 FMA、四则、平方根、符号注入、最值、比较、分类、格式转换、全部 W/WU/L/LU 转换和位搬运均走正式机器码路径；五种舍入、NaN boxing、canonical NaN、次正规数、NV/DZ/OF/UF/NX、FS 门控和保留编码专项测试通过；普通与 ASan/UBSan 完整测试均为 8/8 通过。
  - 对应需求：`ISA-REQ-004`、`CPU-REQ-002`、`CPU-REQ-016`、`CPU-REQ-017`、`CPU-REQ-018`、`CPU-REQ-019`
- [x] **ISA-106** 完整实现 RV64C 解压与执行映射。
  - 实现文件：`include/rvemu/core/compressed_decoder.hpp`、`src/core/compressed_decoder.cpp`、`src/core/cpu.cpp`、`src/core/csr.cpp`
  - 验证命令：`ctest --test-dir build --output-on-failure`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：RV64C 三个 quadrant 的整数、控制流、栈指针访存、双精度压缩访存、六位移位量、负跳转/分支、HINT、reserved 编码、原始 16 位 Trap 和 16/32 位半字交错均通过正式 CPU/Bus/RAM 机器码测试；普通与 ASan/UBSan 完整测试均为 9/9 通过。
  - 对应需求：`ISA-REQ-005`、`ISA-REQ-004`、`CPU-REQ-017`
- [x] **ISA-107** 运行正式 ISA/一致性测试并记录结果。
  - 验证命令：`tools/conformance/run-act4.sh`
  - 验证结果：ACT4 RV64IMAFDC 正式一致性测试总数 348，通过 348，失败或超时 0；完整日志见 `artifacts/logs/act4-rv64imafdc.log`。

## 8. 阶段 6：MMU、Sv39 与 TLB

- [x] **MMU-001** 实现 `satp` Bare/Sv39 模式及规范虚拟地址检查。
  - 实现文件：`include/rvemu/memory/mmu.hpp`、`src/memory/mmu.cpp`
  - 验证命令：`./build/tests/rvemu_mmu_tests`
  - 验证结果：Bare 直通、Sv39 模式识别和非规范 VA 页错误通过。
- [x] **MMU-002** 实现三级页表漫游和物理 PTE 读取。
  - 实现文件：`include/rvemu/memory/mmu.hpp`、`src/memory/mmu.cpp`
  - 验证命令：`./build/tests/rvemu_mmu_tests`
  - 验证结果：三级根表、非叶子 PTE、叶子 PTE 和物理 PTE 小端读取通过。
- [x] **MMU-003** 实现 4 KiB、2 MiB、1 GiB 叶子页和错位超级页错误。
  - 实现文件：`src/memory/mmu.cpp`
  - 验证命令：`./build/tests/rvemu_mmu_tests`
  - 验证结果：4 KiB、2 MiB、1 GiB 地址合成和 2 MiB/1 GiB 错位超级页错误通过。
- [x] **MMU-004** 实现 U/S、R/W/X、SUM、MXR 和有效性权限规则。
  - 实现文件：`src/memory/mmu.cpp`、`src/core/cpu.cpp`、`src/core/cpu_floating.cpp`
  - 验证命令：`./build/tests/rvemu_mmu_tests`；`ctest --test-dir build --output-on-failure`
  - 验证结果：U/S 页权限、R/W/X、SUM、MXR、AMO R/W 联合权限和 CPU 统一访存入口通过。
- [ ] **MMU-005** 完成叶子 PTE 的 A/D 位原子更新父级里程碑。
  - [x] **MMU-005A** 在叶子有效性和权限检查成功后确定 A/D 更新集合。
  - [x] **MMU-005B** 提供唯一的物理总线原子读改写事务，不拆成可插入事件的普通读写。
  - [x] **MMU-005C** 使用原始 PTE 值进行条件比较并原子提交更新。
  - [ ] **MMU-005D** 检测 PTE 在提交前发生变化，并从根页表重新执行漫游。
  - [ ] **MMU-005E** 正确处理 PTE 物理区域不可写、越界和总线更新失败。
  - [ ] **MMU-005F** 保证失败时不填充 TLB、不执行最终访存且不留下部分 PTE 修改。
  - [x] **MMU-005G** 验证加载只置 A，存储同时置 A/D，权限拒绝不修改 A/D。
  - [ ] **MMU-005H** 验证 TLB 填充严格发生在 PTE 更新成功之后。
  - 已验证结果：加载只置 A、存储置 A/D、权限拒绝不修改 A/D；A/D 更新使用物理总线 `compare_exchange`。父级暂不勾选，因为 PTE 竞争变化和更新失败路径仍需专项生产测试。
  - 完成条件：以上子任务全部完成，并通过 PTE 竞争变化、总线失败和无副作用测试。
- [x] **MMU-006** 实现至少 64 项 TLB，标签包含必要地址空间信息。
  - 实现文件：`include/rvemu/memory/mmu.hpp`、`src/memory/mmu.cpp`
  - 验证命令：`./build/tests/rvemu_mmu_tests`
  - 验证结果：TLB 填充、命中旧翻译、ASID/global 标签字段和 64 项容量实现通过代码与测试覆盖。
- [x] **MMU-007** 实现 `SFENCE.VMA` 全局与选择性失效语义。
  - 实现文件：`src/memory/mmu.cpp`、`src/core/cpu.cpp`
  - 验证命令：`./build/tests/rvemu_mmu_tests`
  - 验证结果：按 VA 失效后重新页表漫游并观察新 PTE；CPU 解码 `SFENCE.VMA` 并调用唯一 TLB 失效入口。
- [x] **MMU-008** 分别验证取指、加载和存储页错误的 `cause/tval`。
  - 验证命令：`./build/tests/rvemu_mmu_tests`
  - 验证结果：instruction/load/store page fault 的 cause 与原始虚拟地址 tval 均通过。

## 9. 阶段 7：RVV 1.0

- [x] **RVV-001** 实现 32×VLEN=256 位向量状态及 `vlenb=32`。
  - 完成条件：32 个 32 字节寄存器、`vstart/vxrm/vxsat/vcsr/vl/vtype/vlenb` 的唯一 CSR 状态源、VS=Off 门控、合法写入后的 VS Dirty、只读 `vlenb=32`、复位值和 CSR 权限/别名测试均通过。
  - 冻结边界：支持 `m1/m2/m4/m8` 与标准要求的 `mf2/mf4/mf8`；分数 LMUL 仅接受满足 `SEW ≤ LMUL × ELEN` 的组合。保留或不支持的完整 `vtype` 必须由后续 `vset*` 依据 `RVV-REQ-006` 设置 `vill=1, vl=0`，不得静默降级。
  - 实现文件：`include/rvemu/vector/vector_state.hpp`、`src/vector/vector_state.cpp`、`include/rvemu/core/cpu_state.hpp`、`src/core/cpu_state.cpp`、`include/rvemu/core/csr.hpp`、`src/core/csr.cpp`
  - 验证命令：`cmake --build build --parallel`；`./build/tests/rvemu_vector_state_tests`；`ctest --test-dir build --output-on-failure`；`cmake --build build/sanitize --parallel`；`ctest --test-dir build/sanitize --output-on-failure`
  - 验证结果：严格构建和专项 RVV 状态测试通过；常规与 ASan/UBSan CTest 均为 12/12 通过。
- [x] **RVV-002** 实现 `vsetvl/vsetvli/vsetivli` 和合法 `vtype/vl` 计算。
  - 完成条件：三种 OP-V 配置编码、VS=Off 非法、整数/分数 LMUL、保留位/保留 LMUL 的 `vill=1, vl=0` 提交、AVL 特殊形式、相同 VLMAX 的 `vl` 保持与成功后 `vstart` 清零均由唯一配置模块和 CPU 路径完成。
  - 实现文件：`include/rvemu/vector/vector_configuration.hpp`、`src/vector/vector_configuration.cpp`、`include/rvemu/core/cpu.hpp`、`src/core/cpu.cpp`、`include/rvemu/core/csr.hpp`、`src/core/csr.cpp`、`tests/unit/test_cpu_vector_configuration.cpp`
  - 验证命令：`cmake --build build --parallel`；`./build/tests/rvemu_cpu_vector_configuration_tests`；`ctest --test-dir build --output-on-failure`；`cmake --build build/sanitize --parallel`；`ctest --test-dir build/sanitize --output-on-failure`；`git diff --check`
  - 验证结果：严格构建和专项测试通过；常规与 ASan/UBSan CTest 均为 13/13 通过；补丁格式检查无输出。
- [x] **RVV-003** 实现 SEW、LMUL、寄存器组对齐、`vstart/vxrm/vxsat`。
  - 完成条件：唯一布局层覆盖 e8/e16/e32/e64、整数/分数 LMUL、小端元素定位、整数 LMUL 对齐和 v31 边界；CpuState 的受控元素提交统一标记 VS Dirty；`vstart` 固定 8 位、成功完成清零、`vxrm` 唯一读取和 `vxsat` 黏滞累积均已实现。
  - 实现文件：`include/rvemu/vector/vector_register_group.hpp`、`src/vector/vector_register_group.cpp`、`include/rvemu/vector/vector_state.hpp`、`src/vector/vector_state.cpp`、`include/rvemu/core/cpu_state.hpp`、`src/core/cpu_state.cpp`、`include/rvemu/core/csr.hpp`、`src/core/csr.cpp`、`tests/unit/test_vector_register_group.cpp`
  - 验证命令：`cmake --build build --parallel`；`./build/tests/rvemu_vector_register_group_tests`；`ctest --test-dir build --output-on-failure`；`cmake --build build/sanitize --parallel`；`ctest --test-dir build/sanitize --output-on-failure`；`git diff --check`
  - 验证结果：严格构建和专项测试通过；常规与 ASan/UBSan CTest 均为 14/14 通过；补丁格式检查无输出。
- [ ] **RVV-004** 实现单元步长与跨步向量加载/存储及逐元素异常。
- [ ] **RVV-005** 实现规定范围的整数算术、乘除和掩码语义。
- [ ] **RVV-006** 实现规定范围的向量浮点四则运算和浮点状态更新。
- [ ] **RVV-007** 通过 RVV 边界、tail/mask、重叠和异常重启测试。

## 10. 阶段 8：CLINT、PLIC 与 UART

- [ ] **DEV-001** 实现 CLINT `mtime/mtimecmp/msip` 及中断挂起同步。
- [ ] **DEV-002** 实现 PLIC 优先级、pending、enable、threshold、claim/complete。
- [ ] **DEV-003** 实现 UART THR/RBR/LSR 及必要的 16550A 寄存器行为。
- [ ] **DEV-004** 实现宿主终端 Raw 模式、非阻塞输入和异常退出恢复。
- [ ] **DEV-005** 验证定时器、外部中断和 UART 控制台持续交互。

## 11. 阶段 9：VirtIO 公共层与块设备

- [ ] **VIO-001** 实现 VirtIO MMIO 标识、状态机和 feature 协商。
- [ ] **VIO-002** 实现 split virtqueue 配置、内存布局和队列通知。
- [ ] **VIO-003** 完成描述符链安全解析父级里程碑。
  - [ ] **VIO-003A** 校验 head、next 和间接表中的所有描述符索引范围。
  - [ ] **VIO-003B** 检测描述符链循环、超长链和非法嵌套。
  - [ ] **VIO-003C** 根据设备请求布局校验每段描述符的设备读写方向。
  - [ ] **VIO-003D** 检查 `address + length`、总长度求和及 DMA 范围溢出。
  - [ ] **VIO-003E** 仅在 feature 协商后实现并接受合法间接描述符。
  - [ ] **VIO-003F** 保证整条链验证完成前不执行任何设备 DMA 或宿主 I/O。
  - 完成条件：以上子任务全部完成，恶意描述符输入只能产生受控设备错误。
- [ ] **VIO-004** 完成 available/used ring 索引与提交顺序父级里程碑。
  - [ ] **VIO-004A** 实现 16 位模 2^16 索引差值和 `0xFFFF -> 0x0000` 回绕。
  - [ ] **VIO-004B** 区分单调回绕索引与通过 queue size 计算的 ring 槽位。
  - [ ] **VIO-004C** 检测驱动一次公布的未处理条目数超过队列容量。
  - [ ] **VIO-004D** 按规范顺序读取 descriptor 内容并观察 available idx。
  - [ ] **VIO-004E** 先写 used element，再以正确可见性发布 used idx。
  - [ ] **VIO-004F** 实现通知抑制，并仅在协商后实现 `EVENT_IDX` 回绕判断。
  - [ ] **VIO-004G** 在设备复位时隔离队列代际，禁止旧请求提交到新队列。
  - [ ] **VIO-004H** 让真实块和网络请求持续跨越至少一次完整 16 位索引回绕。
  - 完成条件：以上子任务全部完成，长期压力测试无丢项、重复完成、越界或死锁。
- [ ] **BLK-001** 实现 VirtIO-Blk 请求头、IN/OUT、状态字节和只读策略。
- [ ] **BLK-002** 实现 512 字节扇区的安全宿主文件读写与边界检查。
- [ ] **BLK-003** 验证真实 ext4 镜像读写、完成中断和错误恢复。

## 12. 阶段 10：VirtIO 网络与宿主链路

- [ ] **NET-001** 实现 VirtIO-Net feature、队列及 10/12 字节头协商。
- [ ] **NET-002** 实现来宾 TX 描述符收集、头部处理和 TAP 写入。
- [ ] **NET-003** 实现 TAP 收包、RX 缓冲填充、used ring 和 PLIC 中断。
- [ ] **NET-004** 实现非阻塞事件处理、背压、短读写和包边界保护。
- [ ] **NET-005** 提供经确认的 TAP/网桥/NAT 配置脚本和恢复步骤。
- [ ] **NET-006** 使用真实链路验证 DHCP、ARP、DNS 和 ICMP。

## 13. 阶段 11：引导与运行时

- [ ] **BOOT-001** 实现 BIOS、内核和磁盘参数校验及安全装载。
- [ ] **BOOT-002** 生成与机器模型一致的 FDT，并放置于规定内存位置。
- [ ] **BOOT-003** 设置 OpenSBI 入口寄存器、PC 和 RAM 布局。
- [ ] **RUN-001** 实现规范 CLI 和稳定错误退出码。
  - 完成条件：同时支持省略网络或 `--net none` 的无网络启动，以及 Linux `--net <tap>`；不得在 macOS 创建伪网络后端。
- [ ] **RUN-002** 实现取指、执行、设备 tick、中断检查的唯一主循环。
- [ ] **RUN-003** 实现信号处理、终端恢复、文件和 TAP 资源清理。

## 14. 阶段 12：真实系统验收

- [ ] **SYS-001** 使用真实 OpenSBI 观察并保存 Banner 证据。
- [ ] **SYS-002** 使用真实 Linux 内核完成启动，无 Kernel Panic。
- [ ] **SYS-003** 挂载真实 ext4 rootfs 并进入可交互 Shell。
- [ ] **SYS-003A** 在 macOS 使用 `--net none` 完成真实启动，并在来宾 Shell 执行 `ls /`、`pwd` 和 `cat /proc/cpuinfo`。
- [ ] **SYS-004** 验证 UART 字符和控制组合键由来宾接收。
- [ ] **SYS-005** 在 Linux TAP 档位的来宾执行 `dhclient eth0` 并获取独立 IP。
- [ ] **SYS-006** 在 Linux TAP 档位的来宾解析 `google.com` 并执行 `ping -c 4 google.com`。
- [ ] **SYS-007** 在 Linux TAP 档位确认 4 个 ICMP 响应且丢包率为 0%，保存完整日志和环境说明。
- [ ] **SYS-008** 完成全部需求追踪复核，无伪造、跳过或未声明偏差。

## 15. 文档站与 GitHub Pages

- [ ] **DOCS-001** 审阅并确认项目化 MkDocs 与 GitHub Actions PRD。
  - 规格文件：`docs-site/specs/mkdocs_prd.zh.md`、`docs-site/specs/github_action_prd.zh.md`
- [ ] **DOCS-002** 锁定 MkDocs、Material、i18n 插件和 Python 依赖版本。
- [ ] **DOCS-003** 建立 `docs-site/docs/zh/` 中文 `.zh.md` 相对 symlink 文档树。
- [ ] **DOCS-004** 建立 `docs-site/docs/en/` 真实英文 `.en.md` 文档树，不使用空翻译或中文替代。
- [ ] **DOCS-005** 实现与模拟器规格对应的左侧分层导航。
- [ ] **DOCS-006** 实现顶部简体中文/English 切换并验证对应页面映射。
- [ ] **DOCS-007** 实现失效、绝对、循环和仓库越界 symlink 严格检查。
- [ ] **DOCS-008** 实现 `.github/workflows/docs-pages.yml` 的 PR 验证与 `main` Pages 部署。
- [ ] **DOCS-009** 使用锁定依赖执行本地严格构建并验证桌面/移动导航与链接。
- [ ] **DOCS-010** 在 GitHub Actions 中真实部署 Pages，并核对公开 URL、语言切换和全部导航。

## 16. 任务证据模板

完成任务时在对应条目下追加：

```text
证据：
- 实现文件：<路径>
- 验证命令：<实际执行的完整命令>
- 验证结果：<通过/失败及摘要>
- 日志或报告：<路径>
- 对应需求：<REQ 编号>
- 已知限制：<无，或具体说明>
```
