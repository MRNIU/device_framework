/**
 * @copyright Copyright The device_framework Contributors
 *
 * @brief ACPI 表解析公开接口
 *
 * 用户应通过此头文件使用 ACPI 表解析功能，而非直接包含 detail/ 中的实现文件。
 *
 * @code
 * #include "device_framework/acpi.hpp"
 *
 * auto* rsdp = device_framework::acpi::FindRsdp(search_base);
 * @endcode
 */

#ifndef DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_ACPI_HPP_
#define DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_ACPI_HPP_

#include "device_framework/detail/acpi/acpi.hpp"

namespace device_framework::acpi {
using namespace detail::acpi;  // NOLINT(google-build-using-namespace)
}  // namespace device_framework::acpi

#endif /* DEVICE_FRAMEWORK_INCLUDE_DEVICE_FRAMEWORK_ACPI_HPP_ */
