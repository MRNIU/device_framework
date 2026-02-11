/**
 * @file pci.hpp
 * @brief Virtio PCI 传输层（占位，暂不实现）
 * @copyright Copyright The virtio_driver Contributors
 * @see virtio-v1.2#4.1
 */

#ifndef VIRTIO_DRIVER_TRANSPORT_PCI_HPP_
#define VIRTIO_DRIVER_TRANSPORT_PCI_HPP_

#include "virtio_driver/transport/transport.hpp"

namespace virtio_driver {

/**
 * @brief Virtio PCI 传输层（占位）
 * @todo 实现 PCI Modern (1.0+) 传输层
 * @see virtio-v1.2#4.1
 */
template <VirtioEnvironmentTraits Traits = NullTraits>
class PciTransport final : public Transport<Traits> {
 public:
  [[nodiscard]] auto IsValid() const -> bool override { return false; }
  [[nodiscard]] auto GetDeviceId() const -> uint32_t override { return 0; }
  [[nodiscard]] auto GetVendorId() const -> uint32_t override { return 0; }

  [[nodiscard]] auto GetStatus() const -> uint32_t override { return 0; }
  auto SetStatus(uint32_t /*status*/) -> void override {}

  [[nodiscard]] auto GetDeviceFeatures() -> uint64_t override { return 0; }
  auto SetDriverFeatures(uint64_t /*features*/) -> void override {}

  [[nodiscard]] auto GetQueueNumMax(uint32_t /*queue_idx*/)
      -> uint32_t override {
    return 0;
  }
  auto SetQueueNum(uint32_t /*queue_idx*/, uint32_t /*num*/) -> void override {}
  auto SetQueueDesc(uint32_t /*queue_idx*/, uint64_t /*addr*/)
      -> void override {}
  auto SetQueueAvail(uint32_t /*queue_idx*/, uint64_t /*addr*/)
      -> void override {}
  auto SetQueueUsed(uint32_t /*queue_idx*/, uint64_t /*addr*/)
      -> void override {}
  [[nodiscard]] auto GetQueueReady(uint32_t /*queue_idx*/) -> bool override {
    return false;
  }
  auto SetQueueReady(uint32_t /*queue_idx*/, bool /*ready*/) -> void override {}

  auto NotifyQueue(uint32_t /*queue_idx*/) -> void override {}
  [[nodiscard]] auto GetInterruptStatus() const -> uint32_t override {
    return 0;
  }
  auto AckInterrupt(uint32_t /*ack_bits*/) -> void override {}

  [[nodiscard]] auto ReadConfigU8(uint32_t /*offset*/) const
      -> uint8_t override {
    return 0;
  }
  [[nodiscard]] auto ReadConfigU16(uint32_t /*offset*/) const
      -> uint16_t override {
    return 0;
  }
  [[nodiscard]] auto ReadConfigU32(uint32_t /*offset*/) const
      -> uint32_t override {
    return 0;
  }
  [[nodiscard]] auto ReadConfigU64(uint32_t /*offset*/) const
      -> uint64_t override {
    return 0;
  }
  [[nodiscard]] auto GetConfigGeneration() const -> uint32_t override {
    return 0;
  }
};

}  // namespace virtio_driver

#endif /* VIRTIO_DRIVER_TRANSPORT_PCI_HPP_ */
