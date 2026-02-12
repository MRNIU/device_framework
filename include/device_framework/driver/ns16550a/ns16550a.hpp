/**
 * @copyright Copyright The device_framework Contributors
 */

#ifndef DEVICE_FRAMEWORK_DRIVER_NS16550A_NS16550A_HPP_
#define DEVICE_FRAMEWORK_DRIVER_NS16550A_NS16550A_HPP_

#include <cstdint>
#include <optional>

namespace device_framework::ns16550a {

/**
 * @brief NS16550A 串口驱动
 *
 * 通过 MMIO 访问 NS16550A UART 寄存器，提供字符读写功能。
 * Header-only 实现，使用 volatile 指针直接访问寄存器。
 */
class Ns16550a {
 public:
  /**
   * @brief 构造函数
   * @param dev_addr 设备 MMIO 基地址
   */
  explicit Ns16550a(uint64_t dev_addr) : base_addr_(dev_addr) {
    // disable interrupt
    Write(kRegIER, 0x00);
    // set baud rate
    Write(kRegLCR, 0x80);
    Write(kUartDLL, 0x03);
    Write(kUartDLM, 0x00);
    // set word length to 8-bits
    Write(kRegLCR, 0x03);
    // enable FIFOs
    Write(kRegFCR, 0x07);
    // enable receiver interrupts
    Write(kRegIER, 0x01);
  }

  /// @name 默认构造/析构函数
  /// @{
  Ns16550a() = default;
  Ns16550a(const Ns16550a&) = delete;
  Ns16550a(Ns16550a&&) = default;
  auto operator=(const Ns16550a&) -> Ns16550a& = delete;
  auto operator=(Ns16550a&&) -> Ns16550a& = default;
  ~Ns16550a() = default;
  /// @}

  /**
   * @brief 写入一个字符
   * @param c 待写入的字符
   */
  void PutChar(uint8_t c) const {
    // 等待发送缓冲区空闲 (LSR bit 5 = 1)
    while ((Read(kRegLSR) & (1 << 5)) == 0) {
    }
    Write(kRegTHR, c);
  }

  /**
   * @brief 阻塞式读取一个字符
   * @return 读取到的字符
   */
  [[nodiscard]] auto GetChar() const -> uint8_t {
    // 等待直到接收缓冲区有数据 (LSR bit 0 = 1)
    while ((Read(kRegLSR) & (1 << 0)) == 0) {
    }
    return Read(kRegRHR);
  }

  /**
   * @brief 非阻塞式尝试读取一个字符
   * @return 读取到的字符，如果没有数据则返回 std::nullopt
   */
  [[nodiscard]] auto TryGetChar() const -> std::optional<uint8_t> {
    if ((Read(kRegLSR) & (1 << 0)) != 0) {
      return Read(kRegRHR);
    }
    return std::nullopt;
  }

  /**
   * @brief 检查接收缓冲区是否有数据可读（不消耗数据）
   * @return true 如果有数据可读
   */
  [[nodiscard]] auto HasData() const -> bool {
    return (Read(kRegLSR) & (1 << 0)) != 0;
  }

 private:
  /// @brief 从寄存器读取
  [[nodiscard]] auto Read(uint8_t reg) const -> uint8_t {
    return *reinterpret_cast<volatile uint8_t*>(base_addr_ + reg);
  }

  /// @brief 向寄存器写入
  void Write(uint8_t reg, uint8_t val) const {
    *reinterpret_cast<volatile uint8_t*>(base_addr_ + reg) = val;
  }

  /// read mode: Receive holding reg
  static constexpr uint8_t kRegRHR = 0;
  /// write mode: Transmit Holding Reg
  static constexpr uint8_t kRegTHR = 0;
  /// write mode: interrupt enable reg
  static constexpr uint8_t kRegIER = 1;
  /// write mode: FIFO control Reg
  static constexpr uint8_t kRegFCR = 2;
  /// read mode: Interrupt Status Reg
  static constexpr uint8_t kRegISR = 2;
  /// write mode: Line Control Reg
  static constexpr uint8_t kRegLCR = 3;
  /// write mode: Modem Control Reg
  static constexpr uint8_t kRegMCR = 4;
  /// read mode: Line Status Reg
  static constexpr uint8_t kRegLSR = 5;
  /// read mode: Modem Status Reg
  static constexpr uint8_t kRegMSR = 6;

  /// LSB of divisor Latch when enabled
  static constexpr uint8_t kUartDLL = 0;
  /// MSB of divisor Latch when enabled
  static constexpr uint8_t kUartDLM = 1;

  uint64_t base_addr_ = 0;
};

}  // namespace device_framework::ns16550a

#endif /* DEVICE_FRAMEWORK_DRIVER_NS16550A_NS16550A_HPP_ */
