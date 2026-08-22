#pragma once

#include "core/Types.hpp"
#include "mesh/Quad.hpp"
#include "world/BlockShape.hpp"

namespace mc {

/// One box of a non-cube block, packed into the same 64 bits a `Quad` uses.
///
///   bits  0.. 4   block x    position within the section, 0..31
///   bits  5.. 9   block y
///   bits 10..14   block z
///   bits 15..18   min x      the box inside that cell, in sixteenths, 0..15
///   bits 19..22   min y
///   bits 23..26   min z
///   bits 27..30   size x - 1 so 1..16 sixteenths; a zero-thickness box cannot exist
///   bits 32..35   size y - 1
///   bits 36..39   size z - 1
///   bits 40..46   material   index into kLayers, exactly as a Quad's does
///   bits 47..50   sky light  0..15
///   bits 51..54   block light
///   bits 55..63   spare (9 bits), plus bit 31
///
/// **Sixty-four bits, in the same arena, decoded by a different program.** That is
/// the third time this engine has done it: water reads a Quad's bits 33..40 as corner
/// drops rather than AO, glass shares `chunk.vert` and changes only the fragment rule.
/// A separate arena would mean a second allocator, a second lifetime, a second upload
/// path and a second copy of `SectionMeshStore`'s deferred-reuse frame clock, all to
/// hold the few hundred words a house is worth.
///
/// **A box rather than a face, and that is what differs from `Quad`.** The greedy
/// mesher merges coplanar faces across a whole section, so a face is the unit that
/// pays for itself there. Nothing merges here -- a slab's geometry stops at its own
/// cell -- so one word describing a box that the vertex shader expands to six faces
/// is a sixth of the memory and a sixth of the words to sort through.
///
/// **Light is per box, not per corner, and it is the one thing this format gives up.**
/// A Quad spends sixteen bits on four corners of sky light so a cave mouth fades
/// smoothly; there is no room for that here alongside a box, and the surfaces are
/// small enough that a gradient across one would rarely be visible anyway. Sky and
/// block are kept *separate* rather than pre-combined with `max` the way a Quad's are,
/// because nine bits are spare and a day/night cycle needs exactly that separation
/// (HANDOFF 1.1). The shader combines them for now.
struct ModelBox {
    u64 word = 0;

    static constexpr u32 kMaxCoord = 31;
    static constexpr u32 kMaxSixteenth = 15;
    static constexpr u32 kMaxSize = 16;

    /// `x`, `y`, `z` are the cell within the section. `box` is the geometry inside it.
    static constexpr ModelBox make(u32 x, u32 y, u32 z, const BlockBox& box, u32 material,
                                   u32 skyLight, u32 blockLight) {
        const u32 sizeX = static_cast<u32>(box.maxX) - box.minX;
        const u32 sizeY = static_cast<u32>(box.maxY) - box.minY;
        const u32 sizeZ = static_cast<u32>(box.maxZ) - box.minZ;

        return ModelBox{static_cast<u64>(x)
                        | (static_cast<u64>(y) << 5)
                        | (static_cast<u64>(z) << 10)
                        | (static_cast<u64>(box.minX) << 15)
                        | (static_cast<u64>(box.minY) << 19)
                        | (static_cast<u64>(box.minZ) << 23)
                        | (static_cast<u64>(sizeX - 1) << 27)
                        | (static_cast<u64>(sizeY - 1) << 32)
                        | (static_cast<u64>(sizeZ - 1) << 36)
                        | (static_cast<u64>(material) << 40)
                        | (static_cast<u64>(skyLight) << 47)
                        | (static_cast<u64>(blockLight) << 51)};
    }

    /// Six faces of two triangles, expanded from `gl_VertexID` like everything else
    /// here. There is still no vertex buffer.
    static constexpr u32 kVerticesPerBox = 36;

    /// The same word, carried in the type the mesh arena stores.
    ///
    /// **A cast in name only, and it is deliberate rather than a shortcut.** The arena
    /// holds 64-bit words; `Quad` is what names them because it was the only kind there
    /// was. The model program decodes these words with `ModelBox`'s layout and nothing
    /// else ever reads them, which is the same arrangement the fluid pass already lives
    /// under -- see `Quad::fluidCorners`.
    constexpr Quad asQuad() const {
        Quad quad;
        quad.packed = word;
        return quad;
    }

    constexpr u32 blockX() const { return static_cast<u32>(word & 0x1F); }
    constexpr u32 blockY() const { return static_cast<u32>((word >> 5) & 0x1F); }
    constexpr u32 blockZ() const { return static_cast<u32>((word >> 10) & 0x1F); }
    constexpr u32 minX() const { return static_cast<u32>((word >> 15) & 0xF); }
    constexpr u32 minY() const { return static_cast<u32>((word >> 19) & 0xF); }
    constexpr u32 minZ() const { return static_cast<u32>((word >> 23) & 0xF); }
    constexpr u32 sizeX() const { return static_cast<u32>((word >> 27) & 0xF) + 1; }
    constexpr u32 sizeY() const { return static_cast<u32>((word >> 32) & 0xF) + 1; }
    constexpr u32 sizeZ() const { return static_cast<u32>((word >> 36) & 0xF) + 1; }
    constexpr u32 material() const { return static_cast<u32>((word >> 40) & 0x7F); }
    constexpr u32 skyLight() const { return static_cast<u32>((word >> 47) & 0xF); }
    constexpr u32 blockLight() const { return static_cast<u32>((word >> 51) & 0xF); }
};

static_assert(sizeof(ModelBox) == sizeof(u64),
              "the model pass shares the quad arena, so a box must be one word");

} // namespace mc
