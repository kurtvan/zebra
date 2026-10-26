#include "common/memory.hpp"

#include <mach/mach.h>

namespace gv {

auto PhysicalFootprintBytes() -> uint64_t
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0;
    return info.phys_footprint;
}

} // namespace gv
