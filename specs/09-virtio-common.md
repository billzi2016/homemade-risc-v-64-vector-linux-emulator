# VirtIO MMIO 与 Virtqueue 公共规格

## 1. 标准与传输

- **VIO-REQ-001**：使用冻结版本的 VirtIO MMIO transport，不混用 legacy 与 modern 寄存器语义。
- **VIO-REQ-002**：块设备和网卡共享同一个 VirtIO MMIO 状态机与 split virtqueue 实现。
- **VIO-REQ-003**：设备标识、vendor、version、feature 位和 config generation 必须与实际能力一致。
- **VIO-REQ-004**：未协商的可选能力不得被设备悄悄使用。

具体规范版本在 `SDD-004` 固定；实现和测试必须记录版本。

## 2. MMIO 设备地址

- VirtIO-Blk：`0x10001000..0x10001FFF`。
- VirtIO-Net：`0x10002000..0x10002FFF`。

每个窗口包含独立 transport 状态，但公共寄存器解释来自同一实现。

## 3. 状态机

设备状态至少处理 `ACKNOWLEDGE`、`DRIVER`、`FEATURES_OK`、`DRIVER_OK`、`DEVICE_NEEDS_RESET` 和 `FAILED`。

- 状态必须按规范顺序推进。
- 驱动写 0 执行完整设备复位：停止队列、清中断、清 feature 协商和运行期索引。
- 驱动 feature 不被接受时必须清除/拒绝 `FEATURES_OK`，不能假装支持。
- `DRIVER_OK` 前不得处理队列通知。
- `FAILED` 或需要复位时不得继续 DMA。

## 4. Feature 协商

- 设备只公布已经实现并测试的 feature。
- transport feature 与设备 feature 分页读取/写入。
- `VERSION_1` 等强制位按所选 transport 版本处理。
- 网卡头长度、间接描述符、event index 等能力必须由协商结果驱动，禁止硬编码一种布局后公布另一种 feature。

## 5. Split Virtqueue 布局

队列包含：

- Descriptor Table：每项 16 字节，含 `addr/len/flags/next`。
- Available Ring：含 flags、idx、ring 和可选 used_event。
- Used Ring：含 flags、idx、元素 `{id,len}` 和可选 avail_event。

队列大小必须非零、不超过设备 QueueNumMax，并满足所选规范的幂次约束。desc/avail/used 地址必须正确对齐、无溢出且落在允许 DMA 的 RAM。

## 6. 描述符链解析

- **VIO-REQ-005**：从 available ring 给出的 head 开始，按 NEXT 遍历。
- **VIO-REQ-006**：验证所有索引小于 queue size。
- **VIO-REQ-007**：使用访问计数或 visited 集合检测环路，链长不得超过队列大小。
- **VIO-REQ-008**：按 WRITE 标志验证设备读/写方向。
- **VIO-REQ-009**：INDIRECT 仅在协商后允许，且间接表长度、对齐、嵌套和循环符合规范。
- 所有 `addr + len` 计算必须检查溢出，零长度描述符按规范处理。

解析失败不得执行不受控 DMA。设备应设置失败状态、完成错误请求或报告诊断，具体选择由设备规范决定。

### 6.1 解析与提交分离

- **VIO-REQ-010**：描述符解析阶段只生成经过验证的不可变请求视图，不执行来宾 DMA、磁盘 I/O 或 TAP I/O。
- **VIO-REQ-011**：整条直接或间接描述符链通过索引、方向、长度、溢出和地址范围检查后，设备才可进入执行阶段。
- **VIO-REQ-012**：请求执行失败必须按设备规范完成错误状态或进入需要复位状态，不能重复使用部分解析结果重新提交。

解析器必须统一服务 VirtIO-Blk 和 VirtIO-Net。设备层只声明期望的链布局和各段方向，不得各自复制 NEXT、INDIRECT、循环或溢出检查。

## 7. Ring 索引与内存顺序

`idx` 是 16 位回绕计数器。实现必须用模 2^16 差值判断新条目，不能用普通大小比较。处理流程为：

1. 读取驱动提交的 descriptor 内容。
2. 读取最新 available idx。
3. 处理新 head 和数据 DMA。
4. 写 used element。
5. 发布 used idx。
6. 根据抑制/事件规则决定是否中断。

即使首版单线程，提交顺序也必须在代码结构中明确，避免未来异步后端破坏可见性。

### 7.1 回绕与容量不变量

- **VIO-REQ-013**：待处理条目数按 16 位模运算计算为 `available_idx - last_available_idx`，不得把回绕后的较小数误判为无新请求。
- **VIO-REQ-014**：ring 数组槽位使用回绕索引对 queue size 取模；16 位 idx 本身不得提前按 queue size 截断。
- **VIO-REQ-015**：若待处理数量大于 queue size，视为驱动或队列状态无效，不得覆盖设备尚未处理的历史。
- **VIO-REQ-016**：设备维护自己的 `last_available_idx` 和 used idx；来宾内存中的 idx 不得直接作为宿主容器下标。
- **VIO-REQ-017**：`EVENT_IDX` 判断必须使用 VirtIO 规定的 16 位回绕公式，并且只在协商该 feature 后启用。

### 7.2 可见性与队列代际

- 读取 available idx 前，必须确保随后读取到驱动在发布 idx 前写好的 descriptor 和 ring 元素。
- 写 used idx 前，必须确保 used element 和设备写入的数据已经对来宾可见。
- 设备复位使当前队列代际失效；旧的异步结果不得写入复位后重新配置的 desc/avail/used 地址。
- 用于抑制中断的旧/新索引必须对应同一次提交区间，不能在多个请求之间错误复用。

长期验证必须让真实块请求和真实网络请求分别跨越至少一次完整的 `0xFFFF -> 0x0000` 回绕，并覆盖多次 queue-size 槽位回绕。只执行少量请求不能证明索引逻辑正确。

## 8. 通知与中断

- QueueNotify 只处理已 ready 且合法的指定队列。
- InterruptStatus 分别表达 used buffer 与配置变化原因。
- 驱动写 InterruptACK 只清除指定已置位原因。
- 仍有未确认原因时中断线保持有效。
- used ring 通知抑制和 EVENT_IDX 仅在实现并协商后生效。

## 9. 配置空间一致性

设备配置多字节字段按 VirtIO 小端规则暴露。可能异步变化的配置更新 generation，驱动读取期间可以检测一致性。只读字段写入无副作用。

## 10. 验收条件

- 两种设备通过同一公共 transport/virtqueue 测试套件。
- 覆盖状态乱序、feature 拒绝、复位和 DRIVER_OK 门控。
- 覆盖索引回绕、描述符环、越界、方向错误、间接表和地址溢出。
- 覆盖中断确认与仍有 pending 原因的电平保持。
- 所有恶意来宾输入只导致受控设备错误，不造成宿主越界访问。
- 验证描述符链全部通过后才发生首个 DMA 或宿主 I/O 副作用。
- 验证 available/used idx 跨 16 位回绕时无丢项、重复完成或死锁。
- 验证设备复位后旧队列请求无法污染新队列代际。
