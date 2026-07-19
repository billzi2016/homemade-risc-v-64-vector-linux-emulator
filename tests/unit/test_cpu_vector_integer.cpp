// 文件职责：通过真实 CPU OP-V 机器码验证 RVV 单宽度整数算术、掩码/tail 和除法边界语义。
// 边界：测试不实现替代向量执行器；寄存器布局、CSR 和指令退休均由生产代码完成。

#include "rvemu/bus/bus.hpp"
#include "rvemu/core/cpu.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/vector/vector_configuration.hpp"
#include "rvemu/vector/vector_register_group.hpp"
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
constexpr std::uint64_t kBase=0x80000000ULL;
class F final { public: F():ram(std::make_shared<rvemu::memory::PhysicalMemory>(rvemu::bus::PhysicalAddress{kBase},0x1000U,"rvv-int")),cpu(bus){if(!bus.register_region(ram).ok())throw std::runtime_error("ram");cpu.state().reset(kBase); const auto r=cpu.state().csrs().access({rvemu::core::CsrAddress::Mstatus,rvemu::core::PrivilegeMode::Machine,false,true,rvemu::core::CsrModifyOperation::Replace,1ULL<<9U});if(!r.success)throw std::runtime_error("vs");} rvemu::core::StepResult run(std::uint32_t x){if(!bus.write(rvemu::bus::PhysicalAddress{kBase},rvemu::bus::AccessWidth::Word,x,rvemu::bus::AccessType::Initialization).ok())throw std::runtime_error("write");cpu.state().set_program_counter(kBase);return cpu.step();} rvemu::bus::Bus bus{};std::shared_ptr<rvemu::memory::PhysicalMemory> ram;rvemu::core::Cpu cpu;};
[[nodiscard]] constexpr std::uint32_t vset(std::uint32_t avl,std::uint32_t type){return (3U<<30)|((type&0x3ffU)<<20)|((avl&31U)<<15)|(7U<<12)|0x57U;}
[[nodiscard]] constexpr std::uint32_t op(std::uint32_t f6,std::uint32_t f3,std::uint32_t vd,std::uint32_t vs2,std::uint32_t rs1,bool vm=true){return (f6<<26)|((vm?1U:0U)<<25)|((vs2&31U)<<20)|((rs1&31U)<<15)|(f3<<12)|((vd&31U)<<7)|0x57U;}
[[nodiscard]] rvemu::vector::VectorRegisterGroup group(F& f,std::uint8_t r){const auto c=rvemu::vector::decode_vector_configuration(f.cpu.state().csrs().peek(rvemu::core::CsrAddress::Vtype));const auto g=rvemu::vector::VectorRegisterGroup::create(c,r);if(!g)throw std::runtime_error("group");return *g;}
int failures=0; void ok(bool x,const char* m){if(!x){++failures;std::cerr<<"失败："<<m<<'\n';}}
void test_arithmetic(){F f;ok(f.run(vset(4,0U)).retired,"配置 e8");auto a=group(f,2),b=group(f,4),d=group(f,6);for(std::uint64_t i=0;i<4;++i){ok(f.cpu.state().set_vector_element(a,i,i+1),"写 a");ok(f.cpu.state().set_vector_element(b,i,10+i),"写 b");}ok(f.run(op(0x00,0,6,2,4)).retired,"vadd.vv");ok(f.cpu.state().vector_element(d,3).value_or(0)==17,"vadd 结果");ok(f.run(op(0x00,3,6,2,31)).retired,"vadd.vi -1");ok(f.cpu.state().vector_element(d,0).value_or(0)==0,"vi 符号扩展");f.cpu.state().set_integer(1,0xff);ok(f.run(op(0x25,4,6,2,1)).retired,"vmul.vx");ok(f.cpu.state().vector_element(d,1).value_or(0)==254,"乘法截断");}
void test_mask_tail_division(){F f;ok(f.run(vset(3,0xc0U)).retired,"配置 ta ma");auto a=group(f,2),b=group(f,4),d=group(f,6);for(std::uint64_t i=0;i<4;++i){ok(f.cpu.state().set_vector_element(a,i,i==0?0x80U:8U),"写 dividend");ok(f.cpu.state().set_vector_element(b,i,i==0?0xffU:0U),"写 divisor");}rvemu::core::CpuState::VectorRegister mask{};mask[0]=0x5U;f.cpu.state().set_vector(0,mask);ok(f.run(op(0x21,0,6,2,4,false)).retired,"masked vdiv");ok(f.cpu.state().vector_element(d,0).value_or(0)==0x80U,"min/-1");ok(f.cpu.state().vector_element(d,1).value_or(0)==0xffU,"mask agnostic");ok(f.cpu.state().vector_element(d,2).value_or(0)==0xffU,"除零 signed");ok(f.cpu.state().vector_element(d,3).value_or(0)==0xffU,"tail agnostic");ok(f.cpu.state().csrs().vector_start()==0,"成功清零 vstart");}
}
int main(){try{test_arithmetic();test_mask_tail_division();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}if(failures)return 1;std::cout<<"RVV 整数测试全部通过。\n";return 0;}
