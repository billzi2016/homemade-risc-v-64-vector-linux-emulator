// 文件职责：为 ACT4 的可选覆盖率生成器声明 RV64/FLEN64 测试宽度。
// 边界：本项目当前不运行 SystemVerilog 覆盖率仿真，此文件仅满足 ACT4 配置契约。
// rvtest_config.svh
// David_Harris@hmc.edu 7 September 2024
// SPDX-License-Identifier: Apache-2.0

// This file is needed in the config subdirectory for each config supporting coverage.
// It defines which extensions are enabled for that config.

// Define XLEN, used in covergroups
`define XLEN64
`define FLEN64

// Define relevant addresses
`define CLINT_BASE 64'h02000000

//define extra supported extensions to collect full coverage in Privileged files
