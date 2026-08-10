#pragma once

#include "core/Assert.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp" // Face

namespace mc {

/// One merged face, packed into 64 bits (DESIGN.md 3.7).
///
///   bits  0..5   x        origin within the section, 0..32 inclusive --
///   bits  6..11  y        6 bits because a face can sit on the far plane
///   bits 12..17  z        at coordinate 32, not just 0..31
///   bits 18..23  width    merged extent along the face's first tangent, minus 1
///   bits 24..29  height   merged extent along the second tangent, minus 1
///   bits 30..32  face     Face enum
///   bits 33..40  ao       2 bits per corner
///   bits 41..56  material index into kLayers (world/BlockTable.hpp), not a
///                         BlockId -- grass alone draws three layers for one type
///
/// There is no vertex buffer. Quads live in an SSBO and the vertex shader
/// expands each one into four corners from gl_VertexID.
struct Quad {
    u64 packed = 0;

    static constexpr u32 kMaxExtent = 64;

    static constexpr Quad make(u32 x, u32 y, u32 z,
                               u32 width, u32 height,
                               Face face,
                               u16 material,
                               u8 ao = 0) {
        MC_ASSERT(x < 64 && y < 64 && z < 64);
        MC_ASSERT(width >= 1 && width <= kMaxExtent);
        MC_ASSERT(height >= 1 && height <= kMaxExtent);

        Quad quad;
        quad.packed = static_cast<u64>(x)
                    | (static_cast<u64>(y) << 6)
                    | (static_cast<u64>(z) << 12)
                    | (static_cast<u64>(width - 1) << 18)
                    | (static_cast<u64>(height - 1) << 24)
                    | (static_cast<u64>(face) << 30)
                    | (static_cast<u64>(ao) << 33)
                    | (static_cast<u64>(material) << 41);
        return quad;
    }

    constexpr u32 x() const { return static_cast<u32>(packed & 0x3F); }
    constexpr u32 y() const { return static_cast<u32>((packed >> 6) & 0x3F); }
    constexpr u32 z() const { return static_cast<u32>((packed >> 12) & 0x3F); }
    constexpr u32 width() const { return static_cast<u32>((packed >> 18) & 0x3F) + 1; }
    constexpr u32 height() const { return static_cast<u32>((packed >> 24) & 0x3F) + 1; }
    constexpr Face face() const { return static_cast<Face>((packed >> 30) & 0x7); }
    constexpr u8 ao() const { return static_cast<u8>((packed >> 33) & 0xFF); }
    constexpr u16 material() const { return static_cast<u16>((packed >> 41) & 0xFFFF); }
};

static_assert(sizeof(Quad) == 8, "Quad must stay 8 bytes; the shader reads it as a uvec2");

} // namespace mc
