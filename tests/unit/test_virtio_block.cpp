// 文件职责：用小型临时镜像验证 VirtIO-Blk IN/OUT、状态字节、used ring 和边界错误路径。
// 边界：测试只写入当前构建目录下 1024 字节临时文件，不使用真实用户镜像或大规模写入。

#include "rvemu/bus/address_map.hpp"
#include "rvemu/bus/bus.hpp"
#include "rvemu/devices/virtio_block.hpp"
#include "rvemu/devices/virtio_mmio.hpp"
#include "rvemu/memory/physical_memory.hpp"
#include "rvemu/platform/disk_backend.hpp"

#include <array>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

constexpr std::uint64_t kVirtioBase = rvemu::bus::address_map::kVirtioBlock.base;
constexpr std::uint64_t kRamBase = rvemu::bus::address_map::kDefaultRam.base;
constexpr std::uint64_t kDesc = kRamBase + 0x1000U;
constexpr std::uint64_t kAvail = kRamBase + 0x2000U;
constexpr std::uint64_t kUsed = kRamBase + 0x3000U;
constexpr std::uint64_t kHeader = kRamBase + 0x4000U;
constexpr std::uint64_t kData = kRamBase + 0x5000U;
constexpr std::uint64_t kStatus = kRamBase + 0x6000U;

constexpr std::uint16_t kDescNext = 1U << 0U;
constexpr std::uint16_t kDescWrite = 1U << 1U;

constexpr std::uint64_t kQueueSel = 0x030U;
constexpr std::uint64_t kQueueNum = 0x038U;
constexpr std::uint64_t kQueueReady = 0x044U;
constexpr std::uint64_t kQueueNotify = 0x050U;
constexpr std::uint64_t kStatusReg = 0x070U;
constexpr std::uint64_t kDriverFeatures = 0x020U;
constexpr std::uint64_t kDriverFeaturesSel = 0x024U;
constexpr std::uint64_t kQueueDescLow = 0x080U;
constexpr std::uint64_t kQueueDescHigh = 0x084U;
constexpr std::uint64_t kQueueDriverLow = 0x090U;
constexpr std::uint64_t kQueueDriverHigh = 0x094U;
constexpr std::uint64_t kQueueDeviceLow = 0x0A0U;
constexpr std::uint64_t kQueueDeviceHigh = 0x0A4U;

class TempDisk final {
   public:
    TempDisk() {
        std::array<char, 32> pattern{
            'r', 'v', 'e', 'm', 'u', '-', 'b', 'l', 'k', '-', 'X', 'X', 'X', 'X', 'X', 'X', '\0'};
        fd_ = ::mkstemp(pattern.data());
        if (fd_ < 0) {
            throw std::runtime_error("创建小型临时磁盘失败");
        }
        path_ = pattern.data();
        std::array<std::uint8_t, 1024> bytes{};
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::uint8_t>(index & 0xFFU);
        }
        if (::write(fd_, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) {
            throw std::runtime_error("初始化小型临时磁盘失败");
        }
        static_cast<void>(::close(fd_));
        fd_ = -1;
    }

    ~TempDisk() {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
        }
        if (!path_.empty()) {
            static_cast<void>(::unlink(path_.c_str()));
        }
    }

    [[nodiscard]] const std::string& path() const noexcept {
        return path_;
    }

   private:
    int fd_{-1};
    std::string path_{};
};

void expect(bool condition, const std::string& message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "失败：" << message << '\n';
    }
}

class Fixture final {
   public:
    Fixture()
        : ram(std::make_shared<rvemu::memory::PhysicalMemory>(
              rvemu::bus::PhysicalAddress{kRamBase}, 0x10000U, "virtio-blk-ram")),
          transport(std::make_shared<rvemu::devices::VirtioMmioTransport>(
              rvemu::devices::VirtioMmioConfig{"virtio-blk-test",
                                               kVirtioBase,
                                               rvemu::bus::address_map::kVirtioBlock.size,
                                               rvemu::devices::VirtioDeviceId::Block,
                                               0x554D'4552U,
                                               0U,
                                               1U,
                                               1U,
                                               8U})),
          block(*transport, disk) {
        if (!bus.register_region(ram).ok() || !bus.register_region(transport).ok()) {
            throw std::runtime_error("VirtIO-Blk 测试区域注册失败");
        }
        if (!disk.open_existing(temp.path(), rvemu::platform::DiskOpenMode::ReadWrite).ok()) {
            throw std::runtime_error("打开小型临时磁盘失败");
        }
        configure_transport();
    }

    void configure_transport() {
        write_reg(kDriverFeaturesSel, 1U);
        write_reg(kDriverFeatures, 1U);
        write_reg(kStatusReg, 1U | 2U | 8U | 4U);
        write_reg(kQueueSel, 0U);
        write_reg(kQueueNum, 4U);
        write_reg(kQueueDescLow, static_cast<std::uint32_t>(kDesc));
        write_reg(kQueueDescHigh, static_cast<std::uint32_t>(kDesc >> 32U));
        write_reg(kQueueDriverLow, static_cast<std::uint32_t>(kAvail));
        write_reg(kQueueDriverHigh, static_cast<std::uint32_t>(kAvail >> 32U));
        write_reg(kQueueDeviceLow, static_cast<std::uint32_t>(kUsed));
        write_reg(kQueueDeviceHigh, static_cast<std::uint32_t>(kUsed >> 32U));
        write_reg(kQueueReady, 1U);
    }

    void write_reg(std::uint64_t offset, std::uint32_t value) {
        if (!bus.write(rvemu::bus::PhysicalAddress{kVirtioBase + offset},
                       rvemu::bus::AccessWidth::Word,
                       value,
                       rvemu::bus::AccessType::Store)
                 .ok()) {
            throw std::runtime_error("写 VirtIO MMIO 失败");
        }
    }

    void write_mem(std::uint64_t address, rvemu::bus::AccessWidth width, std::uint64_t value) {
        if (!bus.write(rvemu::bus::PhysicalAddress{address},
                       width,
                       value,
                       rvemu::bus::AccessType::DmaWrite)
                 .ok()) {
            throw std::runtime_error("写 VirtIO-Blk 测试内存失败");
        }
    }

    [[nodiscard]] std::uint64_t read_mem(std::uint64_t address, rvemu::bus::AccessWidth width) {
        return bus
            .read(rvemu::bus::PhysicalAddress{address}, width, rvemu::bus::AccessType::DmaRead)
            .value;
    }

    void descriptor(std::uint16_t index,
                    std::uint64_t address,
                    std::uint32_t length,
                    std::uint16_t flags,
                    std::uint16_t next) {
        const auto base = kDesc + static_cast<std::uint64_t>(index) * 16U;
        write_mem(base, rvemu::bus::AccessWidth::DoubleWord, address);
        write_mem(base + 8U, rvemu::bus::AccessWidth::Word, length);
        write_mem(base + 12U, rvemu::bus::AccessWidth::HalfWord, flags);
        write_mem(base + 14U, rvemu::bus::AccessWidth::HalfWord, next);
    }

    void request_at(std::uint16_t head, std::uint32_t type, std::uint64_t sector, bool data_write) {
        write_mem(kHeader, rvemu::bus::AccessWidth::Word, type);
        write_mem(kHeader + 8U, rvemu::bus::AccessWidth::DoubleWord, sector);
        descriptor(head, kHeader, 16U, kDescNext, 1U);
        descriptor(1U,
                   kData,
                   512U,
                   static_cast<std::uint16_t>(kDescNext | (data_write ? kDescWrite : 0U)),
                   2U);
        descriptor(2U, kStatus, 1U, kDescWrite, 0U);
        write_mem(kAvail + 4U, rvemu::bus::AccessWidth::HalfWord, head);
        write_mem(kAvail + 2U, rvemu::bus::AccessWidth::HalfWord, 1U);
        write_reg(kQueueNotify, 0U);
    }

    void request(std::uint32_t type, std::uint64_t sector, bool data_write) {
        request_at(0U, type, sector, data_write);
    }

    TempDisk temp;
    rvemu::bus::Bus bus{};
    std::shared_ptr<rvemu::memory::PhysicalMemory> ram;
    std::shared_ptr<rvemu::devices::VirtioMmioTransport> transport;
    rvemu::platform::DiskBackend disk;
    rvemu::devices::VirtioBlockDevice block;
};

void test_read_request(int& failures) {
    Fixture fixture;
    fixture.request(rvemu::devices::VirtioBlockDevice::kRequestIn, 0U, true);
    expect(fixture.block.process_one(fixture.bus)
               == rvemu::devices::VirtioBlockProcessStatus::Completed,
           "IN 请求必须完成",
           failures);
    expect(fixture.read_mem(kData, rvemu::bus::AccessWidth::Byte) == 0U
               && fixture.read_mem(kData + 255U, rvemu::bus::AccessWidth::Byte) == 255U,
           "IN 请求必须把磁盘扇区读入来宾缓冲区",
           failures);
    expect(fixture.read_mem(kStatus, rvemu::bus::AccessWidth::Byte)
               == rvemu::devices::VirtioBlockDevice::kStatusOk,
           "IN 请求 status 必须为 OK",
           failures);
    expect(fixture.read_mem(kUsed + 4U, rvemu::bus::AccessWidth::Word) == 0U
               && fixture.read_mem(kUsed + 8U, rvemu::bus::AccessWidth::Word) == 513U
               && fixture.read_mem(kUsed + 2U, rvemu::bus::AccessWidth::HalfWord) == 1U,
           "IN 请求必须发布 used element 和 used idx",
           failures);
}

void test_write_request(int& failures) {
    Fixture fixture;
    for (std::uint64_t offset = 0U; offset < 512U; ++offset) {
        fixture.write_mem(kData + offset, rvemu::bus::AccessWidth::Byte, 0xA5U);
    }
    fixture.request(rvemu::devices::VirtioBlockDevice::kRequestOut, 1U, false);
    expect(fixture.block.process_one(fixture.bus)
               == rvemu::devices::VirtioBlockProcessStatus::Completed,
           "OUT 请求必须完成",
           failures);
    std::vector<std::uint8_t> sector(512U);
    expect(fixture.disk.read(1U, sector).ok() && sector[0] == 0xA5U && sector[511] == 0xA5U,
           "OUT 请求必须写入指定磁盘扇区",
           failures);
    expect(fixture.read_mem(kStatus, rvemu::bus::AccessWidth::Byte)
               == rvemu::devices::VirtioBlockDevice::kStatusOk,
           "OUT 请求 status 必须为 OK",
           failures);
}

void test_out_of_range_reports_ioerr(int& failures) {
    Fixture fixture;
    fixture.request(rvemu::devices::VirtioBlockDevice::kRequestIn, 2U, true);
    expect(fixture.block.process_one(fixture.bus)
               == rvemu::devices::VirtioBlockProcessStatus::Completed,
           "越界 IN 请求也必须以错误 status 完成",
           failures);
    expect(fixture.read_mem(kStatus, rvemu::bus::AccessWidth::Byte)
               == rvemu::devices::VirtioBlockDevice::kStatusIoError,
           "越界请求 status 必须为 IOERR",
           failures);
}

void test_available_head_selects_descriptor_chain(int& failures) {
    Fixture fixture;
    fixture.descriptor(0U, kHeader, 16U, 0U, 0U);
    fixture.request_at(3U, rvemu::devices::VirtioBlockDevice::kRequestIn, 0U, true);
    expect(fixture.block.process_one(fixture.bus)
               == rvemu::devices::VirtioBlockProcessStatus::Completed,
           "设备必须按 available ring head 选择描述符链",
           failures);
    expect(fixture.read_mem(kStatus, rvemu::bus::AccessWidth::Byte)
               == rvemu::devices::VirtioBlockDevice::kStatusOk,
           "available head 指向的请求必须完成为 OK",
           failures);
    expect(fixture.read_mem(kUsed + 4U, rvemu::bus::AccessWidth::Word) == 3U,
           "used element id 必须回写 available head，而不是 descriptor0",
           failures);
}

void test_transport_reset_isolates_old_queue_runtime(int& failures) {
    Fixture fixture;
    fixture.request(rvemu::devices::VirtioBlockDevice::kRequestIn, 0U, true);
    expect(fixture.block.process_one(fixture.bus)
               == rvemu::devices::VirtioBlockProcessStatus::Completed,
           "复位前请求必须先完成",
           failures);

    fixture.write_reg(kStatusReg, 0U);
    fixture.configure_transport();
    fixture.request(rvemu::devices::VirtioBlockDevice::kRequestIn, 0U, true);
    expect(fixture.block.process_one(fixture.bus)
               == rvemu::devices::VirtioBlockProcessStatus::Completed,
           "transport 复位后的新队列请求不得被旧 last_available_idx 卡住",
           failures);
}

}  // namespace

int main() {
    int failures = 0;
    try {
        test_read_request(failures);
        test_write_request(failures);
        test_out_of_range_reports_ioerr(failures);
        test_available_head_selects_descriptor_chain(failures);
        test_transport_reset_isolates_old_queue_runtime(failures);
    } catch (const std::exception& exception) {
        std::cerr << "VirtIO-Blk 测试基础设施失败：" << exception.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
