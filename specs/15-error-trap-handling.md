# 异常、陷阱与错误处理规格

## 1. 错误域

系统必须区分：

1. **来宾同步异常**：非法指令、断点、系统调用、地址错位、访问错误、页错误。
2. **来宾异步中断**：软件、定时器、外部设备中断。
3. **设备请求错误**：无效 VirtIO 链、磁盘越界、未支持请求。
4. **宿主运行错误**：文件、TAP、终端或内存分配失败。
5. **模拟器内部缺陷**：不变量破坏和不可能状态。

这些错误不得统一转换为进程退出或简单布尔失败。

## 2. 同步异常要求

- **TRAP-REQ-001**：异常必须关联产生异常的原始指令 PC。
- **TRAP-REQ-002**：非法指令的 `tval` 按冻结规范保存指令位或零，规则对 16/32 位指令一致可测。
- **TRAP-REQ-003**：地址错位、访问错误和页错误保存相应故障虚拟地址。
- **TRAP-REQ-004**：`ECALL` cause 取决于发起特权级。
- **TRAP-REQ-005**：异常发生时不得提交规范禁止的目标寄存器、PC 或内存副作用。

向量逐元素指令的精确异常例外由 RVV 的 `vstart` 语义控制，已完成元素允许保留。

## 3. 标准 cause 范围

至少正确处理：

- Instruction address misaligned/access fault/illegal instruction/breakpoint。
- Load address misaligned/access fault。
- Store/AMO address misaligned/access fault。
- ECALL from U/S/M。
- Instruction/load/store page fault。
- Supervisor/Machine software、timer、external interrupts。

cause 编码和 interrupt 最高位必须集中定义，不能散落数字。

## 4. 委托与目标选择

- M 模式产生的 Trap 不向低特权委托。
- 在 S/U 模式产生的同步异常根据 `medeleg` 选择 M 或 S 目标。
- 中断根据 `mideleg`、目标 enable、全局位和当前特权选择目标。
- 委托影响目标 CSR 和向量，不应错误修改另一层 `epc/cause/tval`。
- 多个可接收中断按特权规范优先级选择一个。

## 5. Trap 入口

进入 M 模式 Trap：

1. `mepc` 保存故障/被中断 PC。
2. `mcause/mtval` 写入原因和附加值。
3. `MPIE <- MIE`，`MIE <- 0`，`MPP <- 原特权级`。
4. 当前模式切换为 M。
5. PC 设置为 `mtvec` 计算入口。

进入 S 模式使用相应 `sepc/scause/stval/SPIE/SIE/SPP/stvec`。入口地址必须检查模式字段和对齐 WARL 语义。

## 6. Trap 返回

- `MRET` 仅在允许模式合法，恢复 `MIE <- MPIE`、`MPIE <- 1`、模式 <- MPP，并按规范重置 MPP/MPRV。
- `SRET` 受当前特权和 TSR 等适用控制，恢复 S 中断栈和模式。
- 返回 PC 来自对应 epc；错误对齐根据 C 扩展状态处理。
- 非法执行 xRET 必须产生非法指令，而不是宿主错误。

## 7. 异常优先级与部分访问

地址计算、对齐、翻译、权限和物理总线可能同时暴露错误时，必须按规范固定优先级。跨页/跨设备访问不得在后半失败后留下不允许的前半写入；实现应预检或采用可回滚事务。

LR/SC、AMO 和 PTE A/D 更新的异常路径必须保持原子性。SC 失败返回不等同于 Store Trap。

## 8. 设备与宿主错误

- 来宾提交非法块/网请求时，优先按 VirtIO 状态完成或要求设备复位，不崩溃宿主。
- 宿主磁盘或 TAP 永久 I/O 故障必须撤销/完成相关请求并产生清晰诊断；是否继续运行由设备规格定义。
- 终端恢复失败必须报告，但不能阻止尝试关闭其他资源。
- 内部缺陷报告至少包含 PC、特权级、指令、关键 CSR 和最近设备事件摘要。

## 9. 验收条件

- 每种同步异常从 M/S/U 发起并验证委托目标。
- 验证 `epc/cause/tval/status/tvec` 的完整状态转换。
- 验证 Direct/Vectored 中断入口与多个 pending 优先级。
- 验证跨页取指/访存、向量中途异常和原子失败无非法副作用。
- 验证恶意设备请求不会导致宿主越界或未恢复终端。
