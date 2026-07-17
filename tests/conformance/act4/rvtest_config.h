// 文件职责：向 ACT4 汇编环境声明当前 DUT 的可用非特权扩展和 PMP 数量。
// 边界：定义必须与 UDB 配置一致，不能为了生成更多测试而声明未实现能力。

#define RVMODEL_PMP_GRAIN 4
#define RVMODEL_NUM_PMPS 0

#define D_SUPPORTED
#define F_SUPPORTED
#define ZAAMO_SUPPORTED
#define ZALRSC_SUPPORTED
#define ZCA_SUPPORTED
#define ZCD_SUPPORTED
