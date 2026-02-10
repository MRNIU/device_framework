/**
 * @file virtio_blk_test.cpp
 * @brief VirtIO 块设备测试实现
 * @copyright Copyright The virtio_driver Contributors
 */

#include "virtio_blk_test.h"

#include "device/virtio_blk.hpp"
#include "platform_impl.h"
#include "transport/mmio.hpp"
#include "uart.h"
#include "virt_queue/split.hpp"
#include "virtio_mmio_probe.h"

using namespace virtio_driver;
using namespace virtio_driver::blk;

/**
 * @brief 格式化输出容量（转换为 KB/MB/GB）
 */
static void print_capacity(uint64_t sectors) {
  uint64_t bytes = sectors * kSectorSize;
  uint64_t kb = bytes / 1024;
  uint64_t mb = kb / 1024;
  uint64_t gb = mb / 1024;

  if (gb > 0) {
    uart_put_hex(gb);
    uart_puts(" GB");
  } else if (mb > 0) {
    uart_put_hex(mb);
    uart_puts(" MB");
  } else {
    uart_put_hex(kb);
    uart_puts(" KB");
  }
}

/**
 * @brief 打印块设备配置信息
 */
static void print_blk_config(const BlkConfig& config) {
  uart_puts("\n[BLK CONFIG]\n");

  uart_puts("  Capacity: ");
  uart_put_hex(config.capacity);
  uart_puts(" sectors (");
  print_capacity(config.capacity);
  uart_puts(")\n");

  uart_puts("  Max segment size: ");
  uart_put_hex(config.size_max);
  uart_puts(" bytes\n");

  uart_puts("  Max segments: ");
  uart_put_hex(config.seg_max);
  uart_puts("\n");

  uart_puts("  Block size: ");
  uart_put_hex(config.blk_size);
  uart_puts(" bytes\n");

  uart_puts("  Geometry:\n");
  uart_puts("    Cylinders: ");
  uart_put_hex(config.geometry.cylinders);
  uart_puts("\n    Heads: ");
  uart_put_hex(config.geometry.heads);
  uart_puts("\n    Sectors: ");
  uart_put_hex(config.geometry.sectors);
  uart_puts("\n");

  uart_puts("  Topology:\n");
  uart_puts("    Physical block exp: ");
  uart_put_hex(config.topology.physical_block_exp);
  uart_puts("\n    Min I/O size: ");
  uart_put_hex(config.topology.min_io_size);
  uart_puts("\n    Opt I/O size: ");
  uart_put_hex(config.topology.opt_io_size);
  uart_puts("\n");

  uart_puts("  Writeback: ");
  uart_put_hex(config.writeback);
  uart_puts("\n");
}

/**
 * @brief 打印数据块（十六进制格式）
 */
static void print_data_block(const uint8_t* data, size_t len) {
  uart_puts("  Data (first 64 bytes):\n  ");

  size_t print_len = (len > 64) ? 64 : len;
  for (size_t i = 0; i < print_len; i++) {
    if (i > 0 && i % 16 == 0) {
      uart_puts("\n  ");
    }

    // 打印两位十六进制
    uint8_t high = (data[i] >> 4) & 0xF;
    uint8_t low = data[i] & 0xF;

    uart_putc(high < 10 ? '0' + high : 'a' + high - 10);
    uart_putc(low < 10 ? '0' + low : 'a' + low - 10);
    uart_putc(' ');
  }
  uart_puts("\n");
}

/**
 * @brief 等待请求完成（轮询模式）
 */
template <class TransportImpl>
static bool wait_for_request(VirtioBlk<TransportImpl>& blk,
                             int max_iterations = 1000000) {
  for (int i = 0; i < max_iterations; i++) {
    uint32_t processed = blk.process_used();
    if (processed > 0) {
      return true;
    }
    // 简单延迟
    for (int j = 0; j < 100; j++) {
      asm volatile("nop");
    }
  }
  return false;
}

/**
 * @brief 测试 VirtIO 块设备
 */
void test_virtio_blk() {
  uart_puts("\n========================================\n");
  uart_puts("VirtIO Block Device Test\n");
  uart_puts("========================================\n");

  // 1. 扫描设备，查找块设备
  uart_puts("\n[STEP 1] Scanning for block device...\n");

  VirtioDeviceInfo blk_device_info{};
  bool found = false;

  for (uint32_t i = 0; i < VIRTIO_MMIO_MAX_DEVICES; i++) {
    uint64_t base_addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
    VirtioDeviceInfo info;

    if (probe_virtio_device(base_addr, &info)) {
      if (info.device_id == 2) {  // Block device
        blk_device_info = info;
        found = true;
        uart_puts("  Found block device at: ");
        uart_put_hex(base_addr);
        uart_puts("\n");
        break;
      }
    }
  }

  if (!found) {
    uart_puts("[ERROR] No block device found!\n");
    uart_puts(
        "  Please run QEMU with: -drive "
        "file=disk.img,if=none,format=raw,id=hd0 ");
    uart_puts("-device virtio-blk-device,drive=hd0\n");
    return;
  }

  // 2. 获取平台操作接口
  uart_puts("\n[STEP 2] Initializing platform interface...\n");
  auto platform = test_platform::get_platform_ops();

  // 3. 创建传输层
  uart_puts("\n[STEP 3] Creating MMIO transport...\n");
  MmioTransport<> transport(blk_device_info.base_addr);
  uart_puts("  Transport created successfully\n");

  // 4. 分配 virtqueue 内存
  uart_puts("\n[STEP 4] Allocating virtqueue memory...\n");

  uint32_t queue_idx = 0;
  uint16_t queue_max = transport.GetQueueNumMax(queue_idx);
  uart_puts("  Max queue size: ");
  uart_put_hex(queue_max);
  uart_puts("\n");

  // 使用较小的队列大小以节省内存
  uint16_t queue_size = (queue_max > 64) ? 64 : queue_max;
  uart_puts("  Using queue size: ");
  uart_put_hex(queue_size);
  uart_puts("\n");

  size_t vq_mem_size = SplitVirtqueue::calc_size(queue_size);
  uart_puts("  Required memory: ");
  uart_put_hex(vq_mem_size);
  uart_puts(" bytes\n");

  size_t pages_needed =
      (vq_mem_size + platform.page_size - 1) / platform.page_size;
  void* vq_mem = platform.alloc_pages(pages_needed);
  if (vq_mem == nullptr) {
    uart_puts("[ERROR] Failed to allocate virtqueue memory!\n");
    return;
  }

  uint64_t vq_phys = platform.virt_to_phys(vq_mem);
  uart_puts("  Virtqueue memory allocated at: ");
  uart_put_hex(reinterpret_cast<uint64_t>(vq_mem));
  uart_puts(" (phys: ");
  uart_put_hex(vq_phys);
  uart_puts(")\n");

  // 5. 创建 SplitVirtqueue
  uart_puts("\n[STEP 5] Creating SplitVirtqueue...\n");
  auto vq_result =
      SplitVirtqueue::create(vq_mem, vq_mem_size, queue_size, vq_phys);
  if (!vq_result.has_value()) {
    uart_puts("[ERROR] Failed to create virtqueue\n");
    platform.free_pages(vq_mem, pages_needed);
    return;
  }
  auto& vq = vq_result.value();
  uart_puts("  Virtqueue created successfully\n");

  // 6. 创建并初始化块设备
  uart_puts("\n[STEP 6] Initializing VirtIO block device...\n");
  auto blk_result = VirtioBlk<MmioTransport<>>::create(transport, vq, platform);
  if (!blk_result.has_value()) {
    uart_puts("[ERROR] Failed to initialize block device\n");
    platform.free_pages(vq_mem, pages_needed);
    return;
  }
  auto& blk = blk_result.value();
  uart_puts("  Block device initialized successfully\n");

  // 7. 读取并打印设备配置
  uart_puts("\n[STEP 7] Reading device configuration...\n");
  auto config = blk.read_config();
  print_blk_config(config);

  // 8. 测试读取扇区 0
  uart_puts("\n[STEP 8] Testing sector read (sector 0)...\n");

  // 分配数据缓冲区
  uint8_t* read_buffer = reinterpret_cast<uint8_t*>(platform.alloc_pages(1));
  if (read_buffer == nullptr) {
    uart_puts("[ERROR] Failed to allocate read buffer!\n");
    platform.free_pages(vq_mem, pages_needed);
    return;
  }

  BlkReqHeader* read_header =
      reinterpret_cast<BlkReqHeader*>(platform.alloc_pages(1));
  uint8_t* read_status = reinterpret_cast<uint8_t*>(platform.alloc_pages(1));

  if (read_header == nullptr || read_status == nullptr) {
    uart_puts("[ERROR] Failed to allocate request buffers!\n");
    if (read_buffer) platform.free_pages(read_buffer, 1);
    if (read_header) platform.free_pages(read_header, 1);
    if (read_status) platform.free_pages(read_status, 1);
    platform.free_pages(vq_mem, pages_needed);
    return;
  }

  // 发起读请求
  auto read_result = blk.read(0, read_buffer, read_status, read_header);
  if (!read_result.has_value()) {
    uart_puts("[ERROR] Failed to submit read request\n");
  } else {
    uart_puts("  Read request submitted\n");
    uart_puts("  Waiting for completion (polling)...\n");

    if (wait_for_request(blk)) {
      uart_puts("  Request completed!\n");
      uart_puts("  Status: ");
      uart_put_hex(*read_status);

      if (*read_status == static_cast<uint8_t>(BlkStatus::kOk)) {
        uart_puts(" (OK)\n");
        print_data_block(read_buffer, kSectorSize);
      } else if (*read_status == static_cast<uint8_t>(BlkStatus::kIoErr)) {
        uart_puts(" (IO ERROR)\n");
      } else if (*read_status == static_cast<uint8_t>(BlkStatus::kUnsupp)) {
        uart_puts(" (UNSUPPORTED)\n");
      } else {
        uart_puts(" (UNKNOWN)\n");
      }
    } else {
      uart_puts("[ERROR] Request timeout!\n");
    }
  }

  // 9. 测试写入扇区 1
  uart_puts("\n[STEP 9] Testing sector write (sector 1)...\n");

  uint8_t* write_buffer = reinterpret_cast<uint8_t*>(platform.alloc_pages(1));
  BlkReqHeader* write_header =
      reinterpret_cast<BlkReqHeader*>(platform.alloc_pages(1));
  uint8_t* write_status = reinterpret_cast<uint8_t*>(platform.alloc_pages(1));

  if (write_buffer == nullptr || write_header == nullptr ||
      write_status == nullptr) {
    uart_puts("[ERROR] Failed to allocate write buffers!\n");
  } else {
    // 填充测试数据
    for (size_t i = 0; i < kSectorSize; i++) {
      write_buffer[i] = static_cast<uint8_t>(i & 0xFF);
    }

    uart_puts("  Writing test pattern...\n");
    print_data_block(write_buffer, kSectorSize);

    auto write_result = blk.write(1, write_buffer, write_status, write_header);
    if (!write_result.has_value()) {
      uart_puts("[ERROR] Failed to submit write request\n");
    } else {
      uart_puts("  Write request submitted\n");
      uart_puts("  Waiting for completion (polling)...\n");

      if (wait_for_request(blk)) {
        uart_puts("  Request completed!\n");
        uart_puts("  Status: ");
        uart_put_hex(*write_status);

        if (*write_status == static_cast<uint8_t>(BlkStatus::kOk)) {
          uart_puts(" (OK)\n");
        } else if (*write_status == static_cast<uint8_t>(BlkStatus::kIoErr)) {
          uart_puts(" (IO ERROR)\n");
        } else if (*write_status == static_cast<uint8_t>(BlkStatus::kUnsupp)) {
          uart_puts(" (UNSUPPORTED)\n");
        } else {
          uart_puts(" (UNKNOWN)\n");
        }
      } else {
        uart_puts("[ERROR] Request timeout!\n");
      }
    }
  }

  // 10. 验证写入：读回扇区 1
  uart_puts("\n[STEP 10] Verifying write by reading back sector 1...\n");

  uint8_t* verify_buffer = reinterpret_cast<uint8_t*>(platform.alloc_pages(1));
  BlkReqHeader* verify_header =
      reinterpret_cast<BlkReqHeader*>(platform.alloc_pages(1));
  uint8_t* verify_status = reinterpret_cast<uint8_t*>(platform.alloc_pages(1));

  if (verify_buffer == nullptr || verify_header == nullptr ||
      verify_status == nullptr) {
    uart_puts("[ERROR] Failed to allocate verify buffers!\n");
  } else {
    auto verify_result =
        blk.read(1, verify_buffer, verify_status, verify_header);
    if (!verify_result.has_value()) {
      uart_puts("[ERROR] Failed to submit verify read\n");
    } else {
      uart_puts("  Verify read submitted\n");
      uart_puts("  Waiting for completion (polling)...\n");

      if (wait_for_request(blk)) {
        uart_puts("  Request completed!\n");
        uart_puts("  Status: ");
        uart_put_hex(*verify_status);

        if (*verify_status == static_cast<uint8_t>(BlkStatus::kOk)) {
          uart_puts(" (OK)\n");
          print_data_block(verify_buffer, kSectorSize);

          // 比较数据
          bool match = true;
          if (write_buffer != nullptr) {
            for (size_t i = 0; i < kSectorSize; i++) {
              if (verify_buffer[i] != write_buffer[i]) {
                match = false;
                break;
              }
            }

            if (match) {
              uart_puts("\n[SUCCESS] Data verification passed! ✓\n");
            } else {
              uart_puts("\n[ERROR] Data verification failed! ✗\n");
            }
          }
        } else {
          uart_puts(" (ERROR)\n");
        }
      } else {
        uart_puts("[ERROR] Request timeout!\n");
      }
    }
  }

  uart_puts("\n========================================\n");
  uart_puts("VirtIO Block Device Test Completed\n");
  uart_puts("========================================\n\n");

  // 注意：在实际环境中应该释放所有分配的内存
  // 这里为了简化测试代码，省略了清理步骤
}
