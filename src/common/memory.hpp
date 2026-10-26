#pragma once

#include <cstdint>

namespace gv {

/// The process's physical memory footprint in bytes (what Activity Monitor calls "Memory"), including GPU
/// allocations on Apple Silicon. 0 if unavailable.
auto PhysicalFootprintBytes() -> uint64_t;

} // namespace gv
