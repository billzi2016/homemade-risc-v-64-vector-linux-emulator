// 文件职责：把 ACT4 自校验程序的输出与终止状态连接到标准内存 tohost 通道。
// 边界：宏不计算期望值、不替代 CPU 执行，也不访问项目私有 C++ 状态。

#ifndef RVEMU_RVMODEL_MACROS_H
#define RVEMU_RVMODEL_MACROS_H

#define RVMODEL_DATA_SECTION                       \
    .pushsection .tohost,"aw",@progbits;           \
    .align 8; .global tohost; tohost: .dword 0;    \
    .align 8; .global fromhost; fromhost: .dword 0;\
    .popsection

#define RVMODEL_BOOT

// ACT4 约定 1 表示通过、3 表示失败。分两次 32 位写入可以只依赖 RV64I 的 SW，
// 运行器每步轮询同一 RAM 符号，因此不需要额外的测试专用设备或隐藏宿主调用。
#define RVMODEL_HALT_PASS \
    li x1, 1;              \
    la t0, tohost;         \
1:  sw x1, 0(t0);         \
    sw x0, 4(t0);         \
    j 1b

#define RVMODEL_HALT_FAIL \
    li x1, 3;              \
    la t0, tohost;         \
1:  sw x1, 0(t0);         \
    sw x0, 4(t0);         \
    j 1b

#define RVMODEL_IO_INIT(_R1, _R2, _R3)

// ACT4 4.0.0 的通用环境头会无条件检查中断注入宏是否存在，即使当前
// `test_config.yaml` 已关闭特权/中断测试。这里显式定义为无操作，是为了表达
// “当前非特权 ISA 一致性档位不提供外部中断注入后端”，不能作为 CLINT/PLIC、
// PRV 委托或中断抢占语义已经通过的证据。
#define RVMODEL_INTERRUPT_LATENCY 1
#define RVMODEL_TIMER_INT_SOON_DELAY 1
#define RVMODEL_SET_MEXT_INT(_R1, _R2)
#define RVMODEL_CLR_MEXT_INT(_R1, _R2)
#define RVMODEL_SET_MSW_INT(_R1, _R2)
#define RVMODEL_CLR_MSW_INT(_R1, _R2)
#define RVMODEL_SET_SEXT_INT(_R1, _R2)
#define RVMODEL_CLR_SEXT_INT(_R1, _R2)
#define RVMODEL_SET_SSW_INT(_R1, _R2)
#define RVMODEL_CLR_SSW_INT(_R1, _R2)

// HTIF 控制字的高 32 位编码设备 1/命令 1，低字节保存字符。临时寄存器只使用
// ACT4 显式授权的 _R1/_R2/_R3，防止诊断打印破坏被测寄存器上下文。
#define RVMODEL_IO_WRITE_STR(_R1, _R2, _R3, _STR_PTR) \
1:  lbu _R1, 0(_STR_PTR);                             \
    beqz _R1, 2f;                                     \
    la _R2, tohost;                                   \
    sw _R1, 0(_R2);                                   \
    li _R3, 0x01010000;                               \
    sw _R3, 4(_R2);                                   \
    addi _STR_PTR, _STR_PTR, 1;                       \
    j 1b;                                             \
2:

#define RVMODEL_ACCESS_FAULT_ADDRESS 0x00000000

#endif  // RVEMU_RVMODEL_MACROS_H
