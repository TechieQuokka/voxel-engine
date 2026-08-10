#pragma once

#include <cstddef>
#include <cstdint>

namespace mc {

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

/// Cache line size, used to pad away false sharing in lock-free structures.
///
/// A plain constant rather than std::hardware_destructive_interference_size:
/// GCC warns that the standard one's value is not stable across ABI versions,
/// and this project targets x86-64 Linux only, where 64 is correct.
inline constexpr usize kCacheLineSize = 64;

/// Identifies a block type in the global BlockRegistry.
///
/// 16 bits rather than 8: palette compression already removes the per-voxel
/// cost of a wide id, since sections store palette *indices*, not BlockIds.
/// The width here only bounds how many distinct block types can exist.
using BlockId = u16;

inline constexpr BlockId kAirBlock = 0;

/// A function pointer to an OpenGL entry point.
///
/// Declared in core so that `platform` can hand a loader to `rhi` without
/// either module having to include the other's headers, or GLFW's.
using GlProc       = void (*)();
using GlProcLoader = GlProc (*)(const char* name);

} // namespace mc
