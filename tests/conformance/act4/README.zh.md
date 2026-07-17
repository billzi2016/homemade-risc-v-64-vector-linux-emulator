# ACT4 一致性测试配置

本目录描述当前已经实现的单 Hart、非流水线 `RV64IMAFDC_Zicsr_Zifencei`
DUT。特权测试在本节点明确关闭；M/S/U、MMU、中断和 RVV 的正式一致性测试必须在
对应生产模块完成后扩展同一配置，不能以本结果替代。

`riscv-arch-test` 固定为 ACT4 `4.0.0`、提交
`a7c99303516f4e668f7488f172043392e23b9dfd`。该标签的执行代码严格要求 Sail
RISC-V `0.10`，因此本配置遵循代码检查；当前上游 README 提到的 `0.11` 不用于修改
或绕过固定标签的版本校验。

执行入口：

```sh
tools/conformance/fetch-act4.sh
tools/conformance/run-act4.sh
```

生成的测试、ELF、签名和日志全部位于 `artifacts/`。只有日志显示所有生成 ELF
均由 `rvemu_conformance_runner` 实际通过时，`ISA-107` 才能标记完成。
