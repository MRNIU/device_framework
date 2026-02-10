/**
 * @file pci.h
 * @brief Virtio PCI 传输层（占位，暂不实现）
 * @copyright Copyright The virtio_driver Contributors
 * @see virtio-v1.1#4.1
 */

#ifndef VIRTIO_TRANSPORT_PCI_H
#define VIRTIO_TRANSPORT_PCI_H

#include "virtio/transport/transport.h"

namespace virtio {

/**
 * @brief Virtio PCI 传输层（占位）
 * @todo 实现 PCI Modern (1.0+) 传输层
 * @see virtio-v1.1#4.1
 */
class PciTransport final : public Transport {
 public:
  [[nodiscard]] auto GetDeviceId() const -> uint32_t override;
  [[nodiscard]] auto GetVendorId() const -> uint32_t override;

  [[nodiscard]] auto GetStatus() const -> uint32_t override;
  auto SetStatus(uint32_t status) -> void override;

  [[nodiscard]] auto GetDeviceFeatures() const -> uint64_t override;
  auto SetDriverFeatures(uint64_t features) -> void override;

  [[nodiscard]] auto GetQueueNumMax(uint32_t queue_idx) const
      -> uint32_t override;
  auto SetQueueNum(uint32_t queue_idx, uint32_t num) -> void override;
  auto SetQueueDesc(uint32_t queue_idx, uint64_t addr) -> void override;
  auto SetQueueAvail(uint32_t queue_idx, uint64_t addr) -> void override;
  auto SetQueueUsed(uint32_t queue_idx, uint64_t addr) -> void override;
  [[nodiscard]] auto GetQueueReady(uint32_t queue_idx) const -> bool override;
  auto SetQueueReady(uint32_t queue_idx, bool ready) -> void override;

  auto NotifyQueue(uint32_t queue_idx) -> void override;
  [[nodiscard]] auto GetInterruptStatus() const -> uint32_t override;
  auto AckInterrupt(uint32_t ack_bits) -> void override;

  [[nodiscard]] auto ReadConfigU8(uint32_t offset) const -> uint8_t override;
  [[nodiscard]] auto ReadConfigU16(uint32_t offset) const -> uint16_t override;
  [[nodiscard]] auto ReadConfigU32(uint32_t offset) const -> uint32_t override;
  [[nodiscard]] auto ReadConfigU64(uint32_t offset) const -> uint64_t override;
  [[nodiscard]] auto GetConfigGeneration() const -> uint32_t override;
};

} /* namespace virtio */

#endif /* VIRTIO_TRANSPORT_PCI_H */
