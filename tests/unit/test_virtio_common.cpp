// 文件职责：验证 VirtIO MMIO 公共 transport 与 split virtqueue 描述符安全解析的生产路径。
// 边界：测试不实现块设备或网卡请求，不执行宿主文件/TAP I/O，也不复制描述符遍历规则。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/devices/virtio_mmio.hpp"
#include "rvemu/devices/virtqueue.hpp"
#include "rvemu/memory/physical_memory.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kVirtioBase = rvemu::bus::address_map::kVirtioBlock.base;
constexpr std::uint64_t kRamBase = rvemu::bus::address_map::kDefaultRam.base;
constexpr std::uint64_t kDesc = kRamBase + 0x1000U;
constexpr std::uint64_t kAvail = kRamBase + 0x2000U;
constexpr std::uint64_t kUsed = kRamBase + 0x3000U;
constexpr std::uint64_t kBuffer = kRamBase + 0x4000U;
constexpr std::uint64_t kIndirect = kRamBase + 0x4800U;

constexpr std::uint64_t kMagic = 0x000U;
constexpr std::uint64_t kVersion = 0x004U;
constexpr std::uint64_t kDeviceId = 0x008U;
constexpr std::uint64_t kVendorId = 0x00CU;
constexpr std::uint64_t kDeviceFeatures = 0x010U;
constexpr std::uint64_t kDeviceFeaturesSel = 0x014U;
constexpr std::uint64_t kDriverFeatures = 0x020U;
constexpr std::uint64_t kDriverFeaturesSel = 0x024U;
constexpr std::uint64_t kQueueSel = 0x030U;
constexpr std::uint64_t kQueueNumMax = 0x034U;
constexpr std::uint64_t kQueueNum = 0x038U;
constexpr std::uint64_t kQueueReady = 0x044U;
constexpr std::uint64_t kQueueNotify = 0x050U;
constexpr std::uint64_t kStatus = 0x070U;
constexpr std::uint64_t kQueueDescLow = 0x080U;
constexpr std::uint64_t kQueueDescHigh = 0x084U;
constexpr std::uint64_t kQueueDriverLow = 0x090U;
constexpr std::uint64_t kQueueDriverHigh = 0x094U;
constexpr std::uint64_t kQueueDeviceLow = 0x0A0U;
constexpr std::uint64_t kQueueDeviceHigh = 0x0A4U;

constexpr std::uint16_t kDescNext = 1U << 0U;
constexpr std::uint16_t kDescWrite = 1U << 1U;
constexpr std::uint16_t kDescIndirect = 1U << 2U;
constexpr std::uint64_t kFeatureIndirectDesc = 1ULL << 28U;
constexpr std::uint64_t kFeatureEventIdx = 1ULL << 29U;

/** 累积断言失败，使一次执行报告多个 transport/queue 状态偏差。 */
void expect(bool condition, const std::string& message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "失败：" << message << '\n';
    }
}

/** 注册真实 VirtIO transport 和 RAM；所有寄存器与描述符访问都走生产总线。 */
class Fixture final {
   public:
    Fixture()
        : transport(std::make_shared<rvemu::devices::VirtioMmioTransport>(
              rvemu::devices::VirtioMmioConfig{"virtio-test",
                                               kVirtioBase,
                                               rvemu::bus::address_map::kVirtioBlock.size,
                                               rvemu::devices::VirtioDeviceId::Block,
                                               0x554D'4552U,
                                               (1ULL << 5U) | kFeatureIndirectDesc | kFeatureEventIdx,
                                               1U,
                                               2U,
                                               8U})),
          ram(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, 0x10000U, "virtio-test-ram")) {
        if (!bus.register_region(transport).ok() || !bus.register_region(ram).ok()) {
            throw std::runtime_error("VirtIO 测试区域注册失败");
        }
    }

    [[nodiscard]] std::uint32_t read_reg(std::uint64_t offset) {
        return static_cast<std::uint32_t>(
            bus.read(rvemu::bus::PhysicalAddress{kVirtioBase + offset},
                     rvemu::bus::AccessWidth::Word,
                     rvemu::bus::AccessType::Load)
                .value);
    }

    [[nodiscard]] bool write_reg(std::uint64_t offset, std::uint32_t value) {
        return bus.write(rvemu::bus::PhysicalAddress{kVirtioBase + offset},
                         rvemu::bus::AccessWidth::Word,
                         value,
                         rvemu::bus::AccessType::Store)
            .ok();
    }

    void write_mem(std::uint64_t address, rvemu::bus::AccessWidth width, std::uint64_t value) {
        const auto result = bus.write(rvemu::bus::PhysicalAddress{address},
                                      width,
                                      value,
                                      rvemu::bus::AccessType::DmaWrite);
        if (!result.ok()) {
            throw std::runtime_error("写入 VirtIO 测试内存失败");
        }
    }

    void configure_queue(std::uint16_t size = 4U) {
        expect(write_reg(kQueueSel, 0U), "选择 queue0", failures);
        expect(write_reg(kQueueNum, size), "设置 queue size", failures);
        expect(write_reg(kQueueDescLow, static_cast<std::uint32_t>(kDesc)), "写 desc low", failures);
        expect(write_reg(kQueueDescHigh, static_cast<std::uint32_t>(kDesc >> 32U)),
               "写 desc high",
               failures);
        expect(write_reg(kQueueDriverLow, static_cast<std::uint32_t>(kAvail)),
               "写 avail low",
               failures);
        expect(write_reg(kQueueDriverHigh, static_cast<std::uint32_t>(kAvail >> 32U)),
               "写 avail high",
               failures);
        expect(write_reg(kQueueDeviceLow, static_cast<std::uint32_t>(kUsed)), "写 used low", failures);
        expect(write_reg(kQueueDeviceHigh, static_cast<std::uint32_t>(kUsed >> 32U)),
               "写 used high",
               failures);
        expect(write_reg(kQueueReady, 1U), "queue ready", failures);
    }

    void negotiate_common_features(std::uint64_t features) {
        expect(write_reg(kDriverFeaturesSel, 0U),
               "选择 driver feature 低 32 位页",
               failures);
        expect(write_reg(kDriverFeatures, static_cast<std::uint32_t>(features)),
               "写入 driver feature 低 32 位页",
               failures);
        expect(write_reg(kDriverFeaturesSel, 1U),
               "选择 driver feature 高 32 位页",
               failures);
        expect(write_reg(kDriverFeatures, static_cast<std::uint32_t>((features >> 32U) | 1U)),
               "写入 driver feature 高 32 位页和 VERSION_1",
               failures);
        expect(write_reg(kStatus, 1U | 2U | 8U) && read_reg(kStatus) == (1U | 2U | 8U),
               "FEATURES_OK 必须接受请求的公共 feature 子集",
               failures);
    }

    int failures{0};
    rvemu::bus::Bus bus{};
    std::shared_ptr<rvemu::devices::VirtioMmioTransport> transport;
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram;
};

void write_descriptor(Fixture& fixture,
                      std::uint16_t index,
                      std::uint64_t address,
                      std::uint32_t length,
                      std::uint16_t flags,
                      std::uint16_t next) {
    const auto base = kDesc + static_cast<std::uint64_t>(index) * 16U;
    fixture.write_mem(base, rvemu::bus::AccessWidth::DoubleWord, address);
    fixture.write_mem(base + 8U, rvemu::bus::AccessWidth::Word, length);
    fixture.write_mem(base + 12U, rvemu::bus::AccessWidth::HalfWord, flags);
    fixture.write_mem(base + 14U, rvemu::bus::AccessWidth::HalfWord, next);
}

void write_descriptor_at(Fixture& fixture,
                         std::uint64_t table,
                         std::uint16_t index,
                         std::uint64_t address,
                         std::uint32_t length,
                         std::uint16_t flags,
                         std::uint16_t next) {
    const auto base = table + static_cast<std::uint64_t>(index) * 16U;
    fixture.write_mem(base, rvemu::bus::AccessWidth::DoubleWord, address);
    fixture.write_mem(base + 8U, rvemu::bus::AccessWidth::Word, length);
    fixture.write_mem(base + 12U, rvemu::bus::AccessWidth::HalfWord, flags);
    fixture.write_mem(base + 14U, rvemu::bus::AccessWidth::HalfWord, next);
}

/** 验证 MMIO identity、feature 分页、状态顺序、FEATURES_OK 拒绝和复位清理。 */
void test_identity_features_and_status(int& failures) {
    Fixture fixture;
    expect(fixture.read_reg(kMagic) == rvemu::devices::VirtioMmioTransport::kMagicValue,
           "MagicValue 必须为 virt",
           failures);
    expect(fixture.read_reg(kVersion) == 2U, "Version 必须为 modern MMIO 2", failures);
    expect(fixture.read_reg(kDeviceId) == 2U && fixture.read_reg(kVendorId) == 0x554D'4552U,
           "DeviceID/VendorID 必须来自设备配置",
           failures);
    expect(fixture.write_reg(kDeviceFeaturesSel, 1U)
               && (fixture.read_reg(kDeviceFeatures) & 1U) == 1U,
           "高 feature 页必须公布 VERSION_1",
           failures);

    expect(fixture.write_reg(kStatus, 1U | 2U | 8U) && fixture.read_reg(kStatus) == (1U | 2U),
           "未协商 VERSION_1 时 FEATURES_OK 必须被清除",
           failures);
    expect(fixture.write_reg(kDriverFeaturesSel, 1U) && fixture.write_reg(kDriverFeatures, 1U)
               && fixture.write_reg(kStatus, 1U | 2U | 8U)
               && fixture.read_reg(kStatus) == (1U | 2U | 8U),
           "协商 VERSION_1 后 FEATURES_OK 必须保持",
           failures);
    expect(fixture.write_reg(kStatus, 0U) && fixture.read_reg(kStatus) == 0U,
           "写 0 必须复位 status",
           failures);
}

/** 验证队列 ready 前置条件、DRIVER_OK 通知门控和复位后队列代际清理。 */
void test_queue_configuration_and_notify(int& failures) {
    Fixture fixture;
    expect(fixture.read_reg(kQueueNumMax) == 8U, "QueueNumMax 必须暴露上限", failures);
    expect(fixture.write_reg(kQueueNum, 3U) && fixture.write_reg(kQueueReady, 1U)
               && fixture.read_reg(kQueueReady) == 0U,
           "非 2 的幂 queue size 不得 ready",
           failures);
    fixture.configure_queue();
    failures += fixture.failures;
    expect(fixture.read_reg(kQueueReady) == 1U, "合法 queue 必须 ready", failures);

    expect(fixture.write_reg(kQueueNotify, 0U), "DRIVER_OK 前 QueueNotify 写入返回成功", failures);
    std::uint16_t queue = 99U;
    expect(!fixture.transport->pop_notification(queue), "DRIVER_OK 前不得产生通知", failures);
    expect(fixture.write_reg(kDriverFeaturesSel, 1U) && fixture.write_reg(kDriverFeatures, 1U)
               && fixture.write_reg(kStatus, 1U | 2U | 8U | 4U)
               && fixture.write_reg(kQueueNotify, 0U)
               && fixture.transport->pop_notification(queue) && queue == 0U,
           "DRIVER_OK 后 ready queue 通知必须进入待处理队列",
           failures);
    expect(fixture.write_reg(kStatus, 0U) && fixture.read_reg(kQueueReady) == 0U,
           "设备复位必须清除 queue ready",
           failures);
}

/** 验证直接描述符链成功解析只产生不可变视图，不执行数据缓冲 DMA。 */
void test_descriptor_chain_success(int& failures) {
    Fixture fixture;
    fixture.configure_queue();
    failures += fixture.failures;
    write_descriptor(fixture, 0U, kBuffer, 16U, kDescNext, 1U);
    write_descriptor(fixture, 1U, kBuffer + 0x100U, 1U, kDescWrite, 0U);
    const auto parsed = rvemu::devices::parse_descriptor_chain(
        fixture.bus,
        fixture.transport->queue_layout(0U),
        0U,
        {rvemu::devices::VirtqueueDescriptorDirection::DeviceReads,
         rvemu::devices::VirtqueueDescriptorDirection::DeviceWrites});
    expect(parsed.ok() && parsed.segments.size() == 2U, "合法直接链必须解析成功", failures);
    if (parsed.ok() && parsed.segments.size() == 2U) {
        expect(parsed.segments[0].address.value() == kBuffer && !parsed.segments[0].device_writes,
               "第一个 segment 必须保持设备读方向",
               failures);
        expect(parsed.segments[1].length == 1U && parsed.segments[1].device_writes,
               "第二个 segment 必须保持设备写方向",
               failures);
    }
}

/** 验证 head/next 越界、循环、方向错误、地址溢出和 indirect 拒绝路径。 */
void test_descriptor_chain_rejections(int& failures) {
    Fixture fixture;
    fixture.configure_queue();
    failures += fixture.failures;
    expect(rvemu::devices::parse_descriptor_chain(fixture.bus, fixture.transport->queue_layout(0U), 9U)
               .code
               == rvemu::devices::VirtqueueParseErrorCode::IndexOutOfRange,
           "head 越界必须拒绝",
           failures);

    write_descriptor(fixture, 0U, kBuffer, 4U, kDescNext, 9U);
    expect(rvemu::devices::parse_descriptor_chain(fixture.bus, fixture.transport->queue_layout(0U), 0U)
               .code
               == rvemu::devices::VirtqueueParseErrorCode::IndexOutOfRange,
           "next 越界必须拒绝",
           failures);
    write_descriptor(fixture, 0U, kBuffer, 4U, kDescNext, 1U);
    write_descriptor(fixture, 1U, kBuffer + 8U, 4U, kDescNext, 0U);
    expect(rvemu::devices::parse_descriptor_chain(fixture.bus, fixture.transport->queue_layout(0U), 0U)
               .code
               == rvemu::devices::VirtqueueParseErrorCode::ChainLoop,
           "描述符循环必须拒绝",
           failures);
    write_descriptor(fixture, 0U, kBuffer, 4U, kDescWrite, 0U);
    expect(rvemu::devices::parse_descriptor_chain(
               fixture.bus,
               fixture.transport->queue_layout(0U),
               0U,
               {rvemu::devices::VirtqueueDescriptorDirection::DeviceReads})
               .code
               == rvemu::devices::VirtqueueParseErrorCode::DirectionMismatch,
           "方向不匹配必须拒绝",
           failures);
    write_descriptor(fixture, 0U, UINT64_MAX - 1U, 8U, 0U, 0U);
    expect(rvemu::devices::parse_descriptor_chain(fixture.bus, fixture.transport->queue_layout(0U), 0U)
               .code
               == rvemu::devices::VirtqueueParseErrorCode::AddressOverflow,
           "DMA 范围溢出必须拒绝",
           failures);
    write_descriptor(fixture, 0U, kBuffer, 16U, kDescIndirect, 0U);
    expect(rvemu::devices::parse_descriptor_chain(fixture.bus, fixture.transport->queue_layout(0U), 0U)
               .code
               == rvemu::devices::VirtqueueParseErrorCode::IndirectUnsupported,
           "未实现 indirect 时必须受控拒绝",
           failures);
}

/** 验证间接描述符只在协商后接受，且嵌套 indirect 会被公共解析器拒绝。 */
void test_indirect_descriptor_chain(int& failures) {
    Fixture fixture;
    fixture.negotiate_common_features(kFeatureIndirectDesc);
    fixture.configure_queue();
    failures += fixture.failures;
    write_descriptor(fixture, 0U, kIndirect, 32U, kDescIndirect, 0U);
    write_descriptor_at(fixture, kIndirect, 0U, kBuffer, 16U, kDescNext, 1U);
    write_descriptor_at(fixture, kIndirect, 1U, kBuffer + 0x100U, 1U, kDescWrite, 0U);

    auto layout = fixture.transport->queue_layout(0U);
    expect(layout.indirect_enabled, "FEATURES_OK 后 queue layout 必须导出 indirect 协商状态", failures);
    layout.indirect_enabled = false;
    expect(rvemu::devices::parse_descriptor_chain(fixture.bus, layout, 0U).code
               == rvemu::devices::VirtqueueParseErrorCode::IndirectUnsupported,
           "未协商 indirect 时必须拒绝间接描述符",
           failures);
    layout.indirect_enabled = true;
    const auto parsed = rvemu::devices::parse_descriptor_chain(
        fixture.bus,
        layout,
        0U,
        {rvemu::devices::VirtqueueDescriptorDirection::DeviceReads,
         rvemu::devices::VirtqueueDescriptorDirection::DeviceWrites});
    expect(parsed.ok() && parsed.segments.size() == 2U,
           "协商 indirect 后必须解析间接表",
           failures);

    write_descriptor_at(fixture, kIndirect, 0U, kBuffer, 16U, kDescNext, 9U);
    expect(rvemu::devices::parse_descriptor_chain(fixture.bus, layout, 0U).code
               == rvemu::devices::VirtqueueParseErrorCode::IndexOutOfRange,
           "间接表 next 越界必须拒绝",
           failures);

    write_descriptor_at(fixture, kIndirect, 0U, kBuffer, 16U, kDescIndirect, 0U);
    expect(rvemu::devices::parse_descriptor_chain(fixture.bus, layout, 0U).code
               == rvemu::devices::VirtqueueParseErrorCode::IndirectUnsupported,
           "嵌套 indirect 必须拒绝",
           failures);
}

/** 验证 16 位 idx 回绕差值、ring 槽位和 pending 超过 queue size 的拒绝。 */
void test_ring_index_wrap_and_available(int& failures) {
    expect(rvemu::devices::virtqueue_index_delta(0x0000U, 0xFFFFU) == 1U,
           "idx 差值必须支持 0xFFFF 到 0x0000 回绕",
           failures);
    expect(rvemu::devices::virtqueue_ring_slot(0xFFFFU, 8U) == 7U
               && rvemu::devices::virtqueue_ring_slot(0x0000U, 8U) == 0U,
           "ring 槽位必须由完整 idx 对 queue size 取模",
           failures);

    Fixture fixture;
    fixture.configure_queue(4U);
    failures += fixture.failures;
    rvemu::devices::VirtqueueRuntimeState runtime;
    runtime.reset(0xFFFFU);
    fixture.write_mem(kAvail + 2U, rvemu::bus::AccessWidth::HalfWord, 0x0000U);
    fixture.write_mem(kAvail + 4U + 6U, rvemu::bus::AccessWidth::HalfWord, 2U);
    const auto consumed =
        runtime.consume_available(fixture.bus, fixture.transport->queue_layout(0U));
    expect(consumed.ok() && consumed.pending_count == 1U && consumed.head == 2U,
           "consume_available 必须跨 16 位回绕读取旧 idx 对应槽位",
           failures);

    fixture.write_mem(kAvail + 2U, rvemu::bus::AccessWidth::HalfWord, 10U);
    const auto too_many =
        runtime.consume_available(fixture.bus, fixture.transport->queue_layout(0U));
    expect(too_many.code == rvemu::devices::VirtqueueRingErrorCode::TooManyPending,
           "待处理条目数超过 queue size 必须拒绝",
           failures);
}

/** 验证 used element 先写入，再发布 used idx，且复位代际会变化。 */
void test_used_publish_and_generation(int& failures) {
    Fixture fixture;
    fixture.configure_queue(4U);
    failures += fixture.failures;
    rvemu::devices::VirtqueueRuntimeState runtime;
    const auto first_generation = runtime.generation();
    runtime.reset();
    expect(runtime.generation() == first_generation + 1U, "runtime reset 必须推进队列代际", failures);
    const auto published =
        runtime.publish_used(fixture.bus, fixture.transport->queue_layout(0U), 7U, 64U);
    expect(published == rvemu::devices::VirtqueueRingErrorCode::None,
           "publish_used 必须成功写入合法 used ring",
           failures);
    const auto id = fixture.bus
                        .read(rvemu::bus::PhysicalAddress{kUsed + 4U},
                              rvemu::bus::AccessWidth::Word,
                              rvemu::bus::AccessType::DmaRead)
                        .value;
    const auto length = fixture.bus
                            .read(rvemu::bus::PhysicalAddress{kUsed + 8U},
                                  rvemu::bus::AccessWidth::Word,
                                  rvemu::bus::AccessType::DmaRead)
                            .value;
    const auto idx = fixture.bus
                         .read(rvemu::bus::PhysicalAddress{kUsed + 2U},
                               rvemu::bus::AccessWidth::HalfWord,
                               rvemu::bus::AccessType::DmaRead)
                         .value;
    expect(id == 7U && length == 64U && idx == 1U,
           "used element 和 used idx 必须按规范位置可见",
           failures);
}

/** 验证普通 avail flags 和 EVENT_IDX 两种通知抑制路径。 */
void test_notification_suppression(int& failures) {
    Fixture fixture;
    fixture.negotiate_common_features(kFeatureEventIdx);
    fixture.configure_queue(4U);
    failures += fixture.failures;
    rvemu::devices::VirtqueueRuntimeState runtime;
    runtime.reset();
    auto layout = fixture.transport->queue_layout(0U);
    expect(runtime.publish_used(fixture.bus, layout, 1U, 1U)
               == rvemu::devices::VirtqueueRingErrorCode::None,
           "准备 used idx=1",
           failures);
    layout.event_idx_enabled = false;
    bool needed = false;
    expect(runtime.driver_notification_needed(fixture.bus, layout, needed)
               == rvemu::devices::VirtqueueRingErrorCode::None
               && needed,
           "未协商 EVENT_IDX 且 avail flags 未抑制时必须通知",
           failures);
    fixture.write_mem(kAvail, rvemu::bus::AccessWidth::HalfWord, 1U);
    expect(runtime.driver_notification_needed(fixture.bus, layout, needed)
               == rvemu::devices::VirtqueueRingErrorCode::None
               && !needed,
           "未协商 EVENT_IDX 时 avail flags bit0 必须抑制通知",
           failures);

    layout = fixture.transport->queue_layout(0U);
    expect(layout.event_idx_enabled,
           "FEATURES_OK 后 queue layout 必须导出 EVENT_IDX 协商状态",
           failures);
    fixture.write_mem(kAvail + 4U + 4U * 2U, rvemu::bus::AccessWidth::HalfWord, 0U);
    expect(runtime.driver_notification_needed(fixture.bus, layout, needed)
               == rvemu::devices::VirtqueueRingErrorCode::None
               && needed,
           "协商 EVENT_IDX 后 used_event=old 必须触发通知",
           failures);
    fixture.write_mem(kAvail + 4U + 4U * 2U, rvemu::bus::AccessWidth::HalfWord, 1U);
    expect(runtime.driver_notification_needed(fixture.bus, layout, needed)
               == rvemu::devices::VirtqueueRingErrorCode::None
               && !needed,
           "协商 EVENT_IDX 后 used_event=new 必须抑制通知",
           failures);
}

}  // namespace

int main() {
    int failures = 0;
    try {
        test_identity_features_and_status(failures);
        test_queue_configuration_and_notify(failures);
        test_descriptor_chain_success(failures);
        test_descriptor_chain_rejections(failures);
        test_indirect_descriptor_chain(failures);
        test_ring_index_wrap_and_available(failures);
        test_used_publish_and_generation(failures);
        test_notification_suppression(failures);
    } catch (const std::exception& exception) {
        std::cerr << "VirtIO 公共测试基础设施失败：" << exception.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
