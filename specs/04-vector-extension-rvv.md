# RVV 1.0 向量扩展规格

## 1. 固定硬件参数

- **RVV-REQ-001**：实现 32 个向量寄存器 `v0..v31`。
- **RVV-REQ-002**：`VLEN=256` 位，`VLENB=32` 字节，`vlenb` CSR 恒定只读为 32。
- **RVV-REQ-003**：支持 SEW 8、16、32、64。
- **RVV-REQ-004**：支持规范编码的 LMUL，包括 PRD 要求的 1、2、4、8；分数 LMUL 是否启用必须依据 RVV 1.0 与 Linux 工具链需求冻结，不能误译为保留编码。
- **RVV-REQ-005**：所有向量元素按小端来宾内存模型与寄存器位布局解释。

### 1.1 首版 `vtype` 能力冻结

固定 `ELEN=64`、最小 SEW=8 的 RVV 1.0 实现必须支持整数 LMUL `m1=0b000`、
`m2=0b001`、`m4=0b010`、`m8=0b011`，以及分数 LMUL `mf8=0b101`、`mf4=0b110`、
`mf2=0b111`。支持的 SEW 编码为 `e8/e16/e32/e64` 对应 `vsew=0b000..0b011`，但
分数 LMUL 的合法 SEW 受 `SEW ≤ LMUL × ELEN` 约束：`mf8` 仅支持 `e8`，`mf4`
支持 `e8/e16`，`mf2` 支持 `e8/e16/e32`；整数 LMUL 支持全部四种 SEW。

全部保留 `vlmul/vsew` 编码、超出上述分数 LMUL 宽度上限的组合以及任何非零保留高位都
不是可被静默降级的配置：`vsetvli`、`vsetivli` 或 `vsetvl` 请求此类完整 `vtype` 值时，
必须写入 `vtype.vill=1`、将其余可见 `vtype` 位清零，并将 `vl=0`。后续依赖 `vtype`
的向量指令遇到当前 `vill=1` 必须触发非法指令异常。

合法 `vtype` 的低位布局冻结为 `vlmul[2:0]`、`vsew[5:3]`、`vta[6]`、`vma[7]`；除
`XLEN-1` 的 `vill` 外，任何置位的保留高位都会使 `vtype` 非法。这样可避免不同模块
各自解释保留位或分数 LMUL，保持 `VLMAX` 计算的单一事实来源。

## 2. 向量 CSR

- `vl`：当前有效元素数，任何合法设置都不得大于当前 `VLMAX`。
- `vtype`：包含 `vill`、`vma`、`vta`、`vsew` 和 `vlmul`。
- `vstart`：可重启异常的下一元素索引，成功完成指令后按规范清零。
- `vxrm/vxsat/vcsr`：定点舍入和饱和状态。
- `vlenb`：只读硬件常量。

`mstatus.VS`/`sstatus.VS` 为 Off 时执行向量指令必须产生非法指令；修改向量架构状态必须标记 VS Dirty。

向量 CSR 地址冻结为：`vstart=0x008`、`vxsat=0x009`、`vxrm=0x00A`、`vcsr=0x00F`、
`vl=0xC20`、`vtype=0xC21`、`vlenb=0xC22`。`vstart/vxsat/vxrm/vcsr` 是来宾可写的
向量状态；`vl/vtype/vlenb` 是来宾只读 CSR，其中只有后续 `vset*` 的唯一内部配置入口
能够更新 `vl/vtype`。当 `VS=Off` 时，访问这些 CSR 或执行任何向量指令均必须产生
非法指令；`vlenb` 虽为只读常量，也不得绕过该上下文门控。写入可写 CSR 或由 `vset*`
提交合法配置的状态变化必须把 VS 标记为 Dirty。

`misa.V` 只能在项目声明范围内的 RVV 指令、CSR、访存、异常重启和一致性验证均完成后
置位。前置模块可以建立真实向量状态，但不得因部分实现向来宾虚假宣称完整 V 扩展。

为保证单 Hart 复位可重复，向量 CSR 的冻结复位值为：`vl=0`、`vtype.vill=1` 且其余
`vtype` 位为零、`vstart=0`、`vxrm=0`、`vxsat=0`。该默认非法配置要求软件先执行后续
`vset*` 建立合法 SEW/LMUL；它不是对任何具体实现的未声明复位值假设。

## 3. `vset*` 语义

- **RVV-REQ-006**：实现 `vsetvli`、`vsetivli`、`vsetvl`。
- 根据 AVL、SEW、LMUL 和 VLEN 计算 `VLMAX` 与新 `vl`。
- 非法或不支持的 `vtype` 设置 `vill` 并按规范设置 `vl`。
- 必须实现 `rd=x0`、`rs1=x0` 等特殊 AVL 组合，不能一律按普通寄存器值处理。

## 4. 寄存器分组与重叠

- LMUL>1 时目标寄存器号必须满足分组对齐且不得超出 v31。
- 宽化、窄化和掩码指令必须遵守源/目标寄存器重叠限制。
- 非法分组或保留组合触发非法指令，不能截断到可用寄存器。
- `v0` 作为掩码源时按单比特元素布局读取；掩码禁用时所有活动元素视为启用。

## 5. Tail 与 Mask 策略

`vta/vma` 控制尾部和被掩码元素的 agnostic/undisturbed 行为。实现必须：

- 不错误修改 undisturbed 元素。
- 对 agnostic 元素采用规范允许且全项目一致的值策略。
- 区分 prestart、active、inactive 和 tail 元素。
- 保证从 `vstart` 重启时早于 `vstart` 的元素不被再次修改。

## 6. 向量访存

- **RVV-REQ-007**：支持 unit-stride 向量加载和存储。
- **RVV-REQ-008**：支持 strided 向量加载和存储，步长按 XLEN 有符号值解释。
- 支持对齐和非对齐元素访问，但每个字节必须经过统一 MMU/总线语义。
- 每个活动元素独立进行地址计算、翻译和权限检查。
- 地址计算按 XLEN 回绕规则，宿主计算不得溢出为未定义行为。
- 发生元素异常时，精确设置 `vstart`，先前已提交元素保持，后续元素不执行。
- 首版是否支持 fault-only-first、indexed 或 segment 访存必须在 ISA 清单中明确；未实现编码必须非法，而不是静默近似。

## 7. 整数与掩码运算

声明范围至少覆盖 PRD 要求的向量整数加、减、乘、除和掩码控制。必须正确处理：

- `vv/vx/vi` 操作数形式和标量扩展。
- SEW 宽度截断、有符号/无符号比较和除法特殊值。
- 掩码逻辑、产生掩码的比较及 carry/borrow 类操作中 `v0` 的特殊角色。
- 除零、最小负数除以 `-1` 等与标量对应宽度一致的定义结果。

## 8. 向量浮点

- 支持 PRD 要求的向量浮点加、减、乘、除，覆盖 SEW=32/64。
- 使用标量 `frm/fflags`，逐元素异常标志最终按 OR 累积。
- 掩码关闭、尾部和 prestart 元素不得产生浮点异常。
- NaN、无穷、零符号和舍入行为必须符合对应 F/D 语义。

## 9. 译码完整性

向量 opcode 下不能仅靠 funct 子集宽松匹配。译码必须同时校验 `funct6/funct3/vm`、操作数类别、SEW、LMUL、扩展状态及保留位。任何未定义组合都触发非法指令。

## 10. 验收条件

- `vlenb` 恒为 32，所有合法 `vtype` 的 `VLMAX` 计算正确。
- 覆盖 `vl=0`、`vl=VLMAX`、不同 SEW/LMUL、mask/tail 策略。
- 覆盖跨寄存器组边界、非法重叠和非法分组。
- 覆盖向量访存跨页、非对齐、元素中途页错误及 `vstart` 重启。
- 使用适用 RVV 1.0 一致性测试，并记录尚未声明支持的合法指令范围。
