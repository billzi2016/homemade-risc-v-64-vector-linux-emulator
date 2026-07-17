# 标量指令集规格

## 1. 标准范围

- **ISA-REQ-001**：实现 RV64I 基础整数指令。
- **ISA-REQ-002**：实现 M 整数乘除扩展。
- **ISA-REQ-003**：实现 A 原子扩展的 `.W` 与 `.D` 形式。
- **ISA-REQ-004**：实现 F/D 单双精度浮点扩展。
- **ISA-REQ-005**：实现适用于 RV64 的 C 压缩扩展。
- **ISA-REQ-006**：未定义、保留或当前状态不允许的编码必须触发非法指令异常。

具体规范版本在 `SDD-004` 冻结。实现不得混用不同版本中不兼容的编码或语义。

## 2. 取指和长度

1. 从当前虚拟 `pc` 读取 16 位半字。
2. 低两位不为 `11` 时按 16 位压缩指令译码。
3. 低两位为 `11` 时，首版仅接受合法 32 位编码，并继续读取第二个半字。
4. 支持 C 扩展时指令地址必须 2 字节对齐；跨页 32 位取指的两部分必须分别产生精确取指异常。
5. 默认顺序 PC 是原 PC 加实际指令长度；分支和 Trap 使用原 PC 计算规定目标或保存值。

## 3. RV64I

必须覆盖：

- `LUI/AUIPC/JAL/JALR`。
- 条件分支及有符号/无符号比较。
- 字节、半字、字、双字加载与符号/零扩展。
- 字节、半字、字、双字存储。
- 立即数和寄存器整数算术、逻辑、移位和比较。
- RV64 的 `*W` 32 位结果再符号扩展语义。
- `FENCE/FENCE.I` 和系统类指令的正式行为。

移位量屏蔽、加减回绕、`JALR` 目标最低位清零和 `*W` 符号扩展必须逐项测试。

## 4. M 扩展

必须覆盖高/低半乘法和有符号/无符号组合，以及 64 位与 32 位除法/余数。

- 除数为零时返回规范规定结果，不产生宿主除零异常。
- 最小负数除以 `-1` 的溢出结果按规范返回。
- `.W` 结果统一符号扩展到 XLEN。
- 高半乘法必须避免宿主语言未定义溢出行为。

## 5. A 扩展

- 首版单 Hart 的保留集采用 LR 自然对齐操作数覆盖的精确 4 或 8 字节范围；CPU 保存不透明 token，物理范围和有效性由唯一总线保留监视器维护。
- LR 的读取与保留建立、SC 的校验与条件写入都必须分别处于单个总线事务中，不能在两步之间插入 DMA 或其他写入。
- SC 仅在保留仍有效时存储并返回成功，否则不得存储并返回失败。
- SC 无论返回成功、条件失败、对齐异常或访问异常，都必须消费 CPU 持有的 token；一次 LR 最多允许一次后续 SC 尝试。
- 影响保留地址范围的来宾写入、AMO、设备 DMA 或规定事件必须使保留失效。
- AMOSWAP、AMOADD、AMOXOR、AMOAND、AMOOR、AMOMIN/MAX 及无符号变体按 `.W/.D` 语义实现。
- LR/SC/AMO 必须按操作数宽度自然对齐；LR 错位产生 load address misaligned，SC/AMO 错位产生 store/AMO address misaligned，不允许用多个普通访问模拟错位原子操作。
- `.W` 的 LR 和 AMO 写回 rd 时必须把观察到的 32 位旧值符号扩展至 XLEN；写入内存的运算只在低 32 位回绕。SC 只写回零或非零状态码。
- `aq/rl` 位在首版同步、单 Hart、无缓存/写缓冲模型中通过同一地址域的总线事务串行化及显式宿主 acquire/release 栅栏实现；不得将其作为未译码位静默忽略，也不把它错误扩展成跨内存与 MMIO 两个域的完整 `FENCE`。
- 原子操作必须通过总线提供的原子事务完成，不得拆成可被设备事件插入的普通读写。

## 6. F/D 扩展

- F 2.2 实现 `FLW/FSW`，四种 `.S` 融合乘加，`FADD/FSUB/FMUL/FDIV/FSQRT.S`，`FSGNJ/FSGNJN/FSGNJX.S`，`FMIN/FMAX.S`，`FEQ/FLT/FLE.S`，`FCLASS.S`，全部 RV64 有符号/无符号 32/64 位整数转换及 `FMV.X.W/FMV.W.X`。
- D 2.2 实现上述全部 `.D` 对应形式、`FLD/FSD`、`FCVT.S.D/FCVT.D.S` 以及 `FMV.X.D/FMV.D.X`。Zfa、Q、H 等独立扩展编码不因共用 `OP-FP` 空间而被宽松接受。
- 产生舍入结果的指令中，`rm=000..100` 直接选择 RNE/RTZ/RDN/RUP/RMM，`rm=111` 动态读取 `frm`；`rm=101/110` 或动态 `frm=101..111` 必须触发非法指令且不产生任何浮点、整数或内存副作用。符号注入、最值、比较、分类和位搬运的 `funct3` 不得误作 `rm` 解析。
- `fflags` 的 NV/DZ/OF/UF/NX 由每条实际执行的浮点运算按 OR 累积；被非法编码或 FS=Off 拦截的指令不得更新标志。
- 单精度值在 64 位浮点寄存器中必须把上 32 位全部置一形成 NaN box；计算、比较、分类和格式转换读取单精度源时，非法 box 按 `0x7FC00000` canonical quiet NaN 处理且不改写原寄存器。`FSW` 与 `FMV.X.W` 是原始位模式传输，必须保留并使用寄存器低 32 位，不执行 box 检查。
- 除规范单独规定的操作外，NaN 结果使用 canonical NaN。`FMIN/FMAX` 在单个 NaN 输入时返回数值输入、两个 NaN 时返回 canonical NaN，任何 signaling NaN 输入设置 NV；`FEQ` 仅对 signaling NaN 设置 NV，`FLT/FLE` 对任何 NaN 设置 NV。
- 次正规数不得 flush-to-zero；tininess 在舍入后检测。融合乘加必须对完整乘积与加数的精确和只舍入一次，不能拆成乘法和加法两条软件运算。
- 浮点转整数越界、无穷和 NaN 按 F/D 2.2 规定的上下界饱和并设置 NV；仅发生舍入差异且未设置 NV 时设置 NX。RV64 的 32 位转换结果统一符号扩展至 XLEN，包括无符号 `FCVT.WU.S/D`。
- `mstatus.FS=Off` 时，浮点 CSR、加载、存储、运算和搬运都不可执行；任何浮点架构状态写入都必须把已启用的 FS 标记为 Dirty。
- 生产执行路径使用项目内整数软件浮点实现，不读取宿主 `float/double/long double` 或 `fenv`。所有格式共用一个解包、固定宽度精确有效数和 guard/round/sticky 舍入入口，禁止按指令族复制第二套舍入逻辑。

## 7. C 扩展

压缩指令通过唯一解压器映射到一条等价 RV64I/F/D 指令，再进入现有执行路径；解压层不得复制算术、访存或 Trap 语义。

- quadrant 0 实现 `C.ADDI4SPN`、`C.FLD/C.LW/C.LD` 和 `C.FSD/C.SW/C.SD`。
- quadrant 1 实现 `C.NOP/C.ADDI/C.ADDIW/C.LI/C.LUI/C.ADDI16SP`、`C.SRLI/C.SRAI/C.ANDI`、`C.SUB/XOR/OR/AND/SUBW/ADDW` 以及 `C.J/C.BEQZ/C.BNEZ`。
- quadrant 2 实现 `C.SLLI`、`C.FLDSP/C.LWSP/C.LDSP`、`C.JR/C.MV/C.EBREAK/C.JALR/C.ADD` 以及 `C.FSDSP/C.SWSP/C.SDSP`。
- 本机同时声明 D，因此按 C 2.0 强制提供 `C.FLD/C.FSD/C.FLDSP/C.FSDSP`；RV32 专属 `C.FLW/C.FSW/C.JAL` 不得在 RV64 编码位置被误接收。
- CI/CIW/CL/CS/CSS/CA/CB/CJ/CR 的压缩寄存器映射、缩放立即数和符号扩展必须逐位重组；RV64 的 `C.SLLI/SRLI/SRAI` 接受六位 `shamt[5:0]`。
- `C.JALR` 链接地址固定为原 PC+2。C 扩展令 `IALIGN=16`，合法控制流可以到达半字边界，32 位指令也可以从 PC+2 开始取指。
- 规范定义的 HINT 作为无架构状态副作用的指令正常退休；reserved、custom、全零、非法零立即数或非法零目标寄存器编码触发非法指令。
- 压缩指令引发 Trap 时，诊断指令字段保存原始 16 位编码；非法指令 `tval` 也使用该原始编码，不能暴露内部展开后的 32 位位模式。

## 8. 对齐与异常

数据访问是否允许非自然对齐必须作为统一机器策略明确。PRD 要求向量内存支持非对齐，不自动意味着所有标量访问无条件允许。首版应选择并记录：软件模拟非对齐标量访问，或触发 load/store address misaligned；无论选择何者都必须保证跨 MMIO/页边界不会产生部分错误副作用。

异常优先级、`tval` 内容和原 PC 保存遵循 `15-error-trap-handling.md`。实现 C 后取指对齐为 2 字节；外部强制设置的奇地址 PC 仍产生 instruction address misaligned，但合法 J/JALR/branch 目标不会生成奇地址。

## 9. 验收条件

- 每个指令族有合法编码、边界输入和非法编码测试。
- 通过适用的 RISC-V ISA/架构测试套件，版本和补丁可复现。
- 对跨页取指、除零、溢出、NaN boxing、LR/SC 失效和 AMO 原子性有专项测试。
- `misa` 只在对应扩展全部达到规格后声明扩展位。
