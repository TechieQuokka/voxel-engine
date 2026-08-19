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
///   bits 41..56  light    4 bits per corner, sky light 0..15
///   bits 57..63  material index into kLayers (world/BlockTable.hpp), not a
///                         BlockId -- grass alone draws three layers for one type
///
/// **`material` is seven bits, and that is what made smooth lighting fit.** It held
/// sixteen for what is 26 texture layers; moving it to the top of the word freed
/// 41..56 for a light level per corner, which is the difference between lighting
/// that interpolates across a merged quad and a single flat level per face. AO
/// stays a separable field at 33..40, so `setAoStrength()` still works -- folding
/// the two together would have fit as well and cost that.
///
/// **On a quad in the fluid pass, bits 33..40 are not AO. They are four corner
/// drops, two bits each.** The word is exactly full and has been since smooth
/// lighting landed, so a fluid level had nowhere of its own to go -- but ambient
/// occlusion on water is meaningless (vanilla does not shade water with it either),
/// which makes that field eight bits of dead weight on precisely the quads that
/// needed somewhere to put a surface height. Nothing about the packing changes; the
/// water shader reads the same bits as a different thing, and `material` stays a
/// texture layer so a second fluid needs no new machinery.
///
/// A drop is how far *below the top of its block* that corner of the surface sits,
/// as an index into a table the water shader owns. **Zero is a full block**, which
/// is what makes this safe to bolt on: every quad built without thinking about
/// fluids -- including every one `CulledMesher` emits -- keeps drawing water exactly
/// as it did before.
///
/// Two bits per corner is four heights where vanilla has nine, and it is a
/// deliberate floor rather than the target: a seven-block run shows three steps
/// instead of seven. The next two bits per corner would have to come from
/// reinterpreting `material` on fluid quads as well (fluid type in the low bits,
/// corner data in the high ones), which is a real change to what the field means
/// and is worth doing only if play says the steps read as steps.
///
/// There is no vertex buffer. Quads live in an SSBO and the vertex shader
/// expands each one into four corners from gl_VertexID.
struct Quad {
    u64 packed = 0;

    static constexpr u32 kMaxExtent = 64;
    /// What seven bits of material holds.
    static constexpr u16 kMaxMaterial = 127;

    /// Corner drops run 0..3, and 0 means the surface is level with the block top.
    static constexpr u8 kMaxFluidDrop = 3;

    static constexpr Quad make(u32 x, u32 y, u32 z,
                               u32 width, u32 height,
                               Face face,
                               u16 material,
                               u8 ao = 0,
                               u16 light = 0) {
        MC_ASSERT(x < 64 && y < 64 && z < 64);
        MC_ASSERT(width >= 1 && width <= kMaxExtent);
        MC_ASSERT(height >= 1 && height <= kMaxExtent);
        MC_ASSERT(material <= kMaxMaterial);

        Quad quad;
        quad.packed = static_cast<u64>(x)
                    | (static_cast<u64>(y) << 6)
                    | (static_cast<u64>(z) << 12)
                    | (static_cast<u64>(width - 1) << 18)
                    | (static_cast<u64>(height - 1) << 24)
                    | (static_cast<u64>(face) << 30)
                    | (static_cast<u64>(ao) << 33)
                    | (static_cast<u64>(light) << 41)
                    | (static_cast<u64>(material) << 57);
        return quad;
    }

    constexpr u32 x() const { return static_cast<u32>(packed & 0x3F); }
    constexpr u32 y() const { return static_cast<u32>((packed >> 6) & 0x3F); }
    constexpr u32 z() const { return static_cast<u32>((packed >> 12) & 0x3F); }
    constexpr u32 width() const { return static_cast<u32>((packed >> 18) & 0x3F) + 1; }
    constexpr u32 height() const { return static_cast<u32>((packed >> 24) & 0x3F) + 1; }
    constexpr Face face() const { return static_cast<Face>((packed >> 30) & 0x7); }
    constexpr u8 ao() const { return static_cast<u8>((packed >> 33) & 0xFF); }
    /// The same eight bits, named for what they mean on a fluid quad.
    constexpr u8 fluidCorners() const { return ao(); }
    /// One corner's drop, 0..3, in the corner order AO and light already use.
    ///
    /// `ao()` is widened explicitly rather than left to integer promotion, which
    /// would make it an `int` and turn the mask into a signed-to-unsigned
    /// conversion. Harmless in value, and `-Wsign-conversion` is an error here --
    /// it broke the asan preset's *build* while debug kept passing, because debug
    /// had no reason to recompile this header.
    constexpr u8 fluidDrop(u32 corner) const {
        return static_cast<u8>((static_cast<u32>(ao()) >> (2u * corner)) & 0x3u);
    }
    constexpr u16 light() const { return static_cast<u16>((packed >> 41) & 0xFFFF); }
    constexpr u16 material() const { return static_cast<u16>((packed >> 57) & 0x7F); }
};

static_assert(sizeof(Quad) == 8, "Quad must stay 8 bytes; the shader reads it as a uvec2");

} // namespace mc
