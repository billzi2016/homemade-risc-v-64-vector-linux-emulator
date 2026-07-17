# CLINT、PLIC 与中断规格

## 1. 中断体系

首版单 Hart 平台至少支持机器软件中断、机器定时器中断、机器外部中断，以及经 `mideleg` 委托后对应的监督级中断路径。pending、enable、全局使能、委托和优先级必须共同决定最终 Trap。

## 2. CLINT

- **INT-REQ-001**：CLINT 映射于 `0x02000000..0x0200BFFF`。
- **INT-REQ-002**：单 Hart 至少提供 `msip`、`mtimecmp` 和 `mtime` 的兼容寄存器布局。
- **INT-REQ-003**：当 `mtime >= mtimecmp` 时 MTIP 置位；写入较大 `mtimecmp` 后应撤销。
- **INT-REQ-004**：`msip` 有效位控制 MSIP，其余写入位按规范忽略或保留策略处理。
- **INT-REQ-005**：`mtime` 来源单调，频率与 FDT `timebase-frequency` 一致。

在 32 位分段访问 64 位计时寄存器时，必须处理 Linux 使用的安全更新序列，不能因为中间低半写入产生不可控的瞬时中断。

## 3. PLIC 中断源

至少为以下设备分配稳定且写入 FDT 的非零 source ID：

- UART 16550A。
- VirtIO-Blk。
- VirtIO-Net。

ID 0 永远表示“无中断”，不能分配给设备。具体 ID 在引导规格中冻结并保持单一常量来源。

## 4. PLIC 状态

- **INT-REQ-006**：实现每源 priority、pending 位和每 context enable 位。
- **INT-REQ-007**：实现每 context threshold 与 claim/complete。
- **INT-REQ-008**：claim 返回高于 threshold 的最高优先级已启用 pending 源；同优先级按较小 ID 选择。
- **INT-REQ-009**：claim 对所选源产生规范规定的 pending/占用状态变化；complete 只接受相应 context 正在处理的合法源。
- **INT-REQ-010**：电平触发源在条件仍为真且完成后必须重新可见，不能永久丢失。

首版至少提供 M-mode 和 S-mode 外部中断 context，使 OpenSBI 可将平台中断委托给 Linux。

## 5. MMIO 行为

- 只允许 PLIC 规格支持的访问宽度和自然对齐。
- 未实现 source 的寄存器位读零、写忽略，但不得影响其他位。
- priority 0 禁用该源；最大优先级为机器模型固定 WARL 值。
- 对无效 claim ID 的 complete 写入按规范安全忽略并可输出受控诊断。

## 6. 设备中断接口

设备不直接写 `mip/sip`。设备设置/撤销自身中断线，PLIC 汇总后驱动 MEIP/SEIP。CLINT 单独驱动软件与定时器 pending。CPU 在统一同步点采样这些线路并更新可见 CSR 状态。

## 7. 中断选择

- 同步异常与中断只在定义的指令边界竞争。
- 中断必须先满足局部 enable 和对应全局使能规则。
- 高特权目标中断在低特权运行时可按规范抢占，不错误依赖低特权全局位。
- 多个中断同时可用时使用特权规范规定的优先级。
- 选择结果交给统一 Trap 入口，不允许每个设备自行跳转 PC。

## 8. 验收条件

- 验证 `mtime` 交叉 `mtimecmp` 的置位与撤销。
- 验证软件中断写入、清除和委托路径。
- 验证 PLIC priority/enable/threshold 的所有组合和同优先级裁决。
- 验证 claim/complete、电平重触发和多个设备并发 pending。
- 验证最终 `mcause/scause`、`mepc/sepc`、`tvec` 入口和中断使能栈。
