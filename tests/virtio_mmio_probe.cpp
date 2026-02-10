/**
 * @copyright Copyright The virtio_driver Contributors
 */

#include "virtio_mmio_probe.h"

#include "uart.h"

void scan_virtio_devices() {
  uart_puts("\n[SCAN] Scanning VirtIO MMIO devices...\n");
  uart_puts("  Base address: ");
  uart_put_hex(VIRTIO_MMIO_BASE);
  uart_puts("\n");

  uint32_t found_count = 0;

  for (uint32_t i = 0; i < VIRTIO_MMIO_MAX_DEVICES; i++) {
    uint64_t base_addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
    VirtioDeviceInfo info;

    if (probe_virtio_device(base_addr, &info)) {
      found_count++;
      uart_puts("\n[DEVICE ");
      uart_put_hex(found_count);
      uart_puts("]\n");
      uart_puts("  Address: ");
      uart_put_hex(info.base_addr);
      uart_puts("\n  Device ID: ");
      uart_put_hex(info.device_id);
      uart_puts(" (");
      uart_puts(get_device_type_name(info.device_id));
      uart_puts(")\n");
      uart_puts("  Vendor ID: ");
      uart_put_hex(info.vendor_id);
      uart_puts("\n  Version: ");
      uart_put_hex(info.version);
      uart_puts("\n  IRQ: ");
      uart_put_hex(info.irq);
      uart_puts("\n");
    }
  }

  if (found_count == 0) {
    uart_puts("[SCAN] No VirtIO devices found.\n");
  } else {
    uart_puts("\n[SCAN] Found ");
    uart_put_hex(found_count);
    uart_puts(" VirtIO device(s).\n");
  }
}
