# VirtIO-Net 与 TAP 规格

## 1. 链路边界

来宾侧呈现 VirtIO-Net，宿主侧绑定用户指定的 TAP 接口名，例如 `tap0`。`/dev/net/tun` 是 Linux 宿主系统接口，不是仓库路径。最终验收必须使用真实 TAP、网桥或 NAT 链路。

## 2. 设备配置

- **NET-REQ-001**：公布稳定、合法且本地管理的 MAC 地址；可配置策略必须写入 FDT/设备配置一致来源。
- **NET-REQ-002**：至少提供 RX queue 0 和 TX queue 1，队列顺序符合 VirtIO-Net 规范。
- **NET-REQ-003**：只公布已实现的 checksum、GSO、mergeable buffer、MAC/status 等 feature。
- **NET-REQ-004**：VirtIO net header 长度由协商 feature 决定，支持范围内正确处理 10 或 12 字节布局。

首版可以不提供硬件 offload，但此时不能公布相关 feature，发送到 TAP 的帧必须已经是有效以太网帧。

## 3. TAP 初始化

1. 打开 Linux `/dev/net/tun`。
2. 使用 `TUNSETIFF` 请求 `IFF_TAP | IFF_NO_PI` 并绑定精确接口名。
3. 验证返回名称与配置一致。
4. 设置非阻塞和 close-on-exec。
5. 不擅自创建、删除或重新配置宿主网桥；所需配置由经确认脚本完成。

权限不足、接口不存在或类型错误必须在切换终端 Raw 模式前报告并退出。

## 4. TX：来宾到宿主

- **NET-REQ-005**：从 TX available ring 获取描述符链。
- 按协商长度解析并验证 VirtIO net header。
- 收集后续只读描述符形成单个以太网帧。
- 若未协商 offload，拒绝含未支持 offload 请求的头，而不是错误转发。
- 将不含 VirtIO header 的完整以太网帧写入 TAP。
- 处理非阻塞暂时不可写、`EINTR` 和短写；一个 TAP write 应保持单帧语义。
- 成功或规范允许的错误处理后更新 used ring 并通知来宾。

## 5. RX：宿主到来宾

- **NET-REQ-006**：事件循环检测 TAP 可读后，一次读取一个完整以太网帧。
- 在有可用 RX 描述符前不得覆盖来宾内存；缺少缓冲时采用有界排队或明确丢包计数策略。
- 构造与协商 feature 一致的 VirtIO net header。
- 将 header 和帧按描述符容量顺序写入设备可写缓冲区。
- 未协商 mergeable buffers 时，一个包必须能装入单个可用链，否则不允许部分交付。
- 完成 used ring 后置中断状态，并通过 PLIC 通知。

## 6. 帧和资源限制

- 定义最大帧长度，至少覆盖标准 MTU 1500 的以太网帧。
- 对 runt、超长帧和宿主异常读取有明确丢弃与计数。
- 所有包缓冲和待发送队列有硬上限，防止来宾或宿主流量耗尽内存。
- 来宾描述符总长度求和必须检查溢出。
- 设备复位时清空未完成队列并撤销中断，不能在新一代队列上提交旧包。

## 7. 事件循环与公平性

CPU、TAP RX、UART 和 VirtIO 队列必须在同一事件循环中获得有界处理机会。持续网络流量不能饿死 CPU，CPU 忙循环也不能无限期阻塞 TAP。可使用 `poll/ppoll/epoll`，但实际选择需保证终端与 TAP 均为非阻塞。

## 8. 网络可观察性

诊断可记录包计数、字节数、丢包原因和队列错误，但默认不得打印完整用户数据。任何抓包功能必须显式启用，并将输出放在被忽略的相对路径如 `artifacts/logs/`。

## 9. 验收条件

- Linux 识别设备为 `eth0`，MAC 和链路状态正确。
- 验证 ARP、DHCP、IPv4、DNS 和 ICMP 的双向真实包流。
- 验证 RX 缓冲不足、TX 背压、短 I/O、队列复位和持续流量。
- 最终由来宾执行 `dhclient eth0` 和 `ping -c 4 google.com`，不能由宿主代执行。
