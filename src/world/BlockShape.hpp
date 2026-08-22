#pragma once

#include "core/Types.hpp"
#include "world/BlockTable.hpp"
#include "world/Coords.hpp"

#include <array>
#include <span>

namespace mc {

/// An axis-aligned box inside one block cell, measured in sixteenths of a block.
///
/// **Sixteenths and integers, not floats, and both halves of that matter.**
/// Sixteenths because it is the resolution vanilla models are authored at and the
/// resolution the model pass packs into a quad word (four bits an axis), so a shape
/// that collides and the shape that draws cannot disagree by rounding. Integers
/// because these are compared for equality in tests and used to build float bounds at
/// the use site: a box that is exactly half a block is `8`, never `0.5f` accumulated
/// from somewhere.
///
/// A full cube is `{0, 0, 0, 16, 16, 16}`. The bounds are min-inclusive and
/// max-exclusive in the same sense a unit cube is: `max` is where the surface is, so a
/// bottom slab's top surface is at `8`, which is `0.5` in world units.
struct BlockBox {
    u8 minX = 0;
    u8 minY = 0;
    u8 minZ = 0;
    u8 maxX = 16;
    u8 maxY = 16;
    u8 maxZ = 16;

    static constexpr f32 kUnit = 1.0f / 16.0f;

    /// The box's top surface, as a fraction of a block. What a player stands on.
    constexpr f32 topFraction() const noexcept { return static_cast<f32>(maxY) * kUnit; }

    constexpr f32 lowX() const noexcept { return static_cast<f32>(minX) * kUnit; }
    constexpr f32 lowY() const noexcept { return static_cast<f32>(minY) * kUnit; }
    constexpr f32 lowZ() const noexcept { return static_cast<f32>(minZ) * kUnit; }
    constexpr f32 highX() const noexcept { return static_cast<f32>(maxX) * kUnit; }
    constexpr f32 highY() const noexcept { return static_cast<f32>(maxY) * kUnit; }
    constexpr f32 highZ() const noexcept { return static_cast<f32>(maxZ) * kUnit; }

    constexpr bool operator==(const BlockBox&) const noexcept = default;
};

namespace detail {

inline constexpr std::array<BlockBox, 1> kCubeBoxes{BlockBox{0, 0, 0, 16, 16, 16}};
inline constexpr std::array<BlockBox, 1> kSlabBottomBoxes{BlockBox{0, 0, 0, 16, 8, 16}};
inline constexpr std::array<BlockBox, 1> kSlabTopBoxes{BlockBox{0, 8, 0, 16, 16, 16}};

} // namespace detail

/// The boxes a block occupies in its own cell, for collision and for standing on.
///
/// **Empty means nothing to stand on and nothing to walk into**, which is air, water
/// and anything else `isSolidBlock` calls not solid. That keeps one answer to "is
/// there something here" rather than two that can drift: this function is the shape,
/// `isSolidBlock` is the flag, and the shape is derived from the flag rather than
/// beside it.
///
/// Returns a span into static storage, so it is free to call in a loop and the result
/// outlives any caller.
constexpr std::span<const BlockBox> blockBoxes(BlockId id) {
    if (!isSolidBlock(id)) {
        return {};
    }
    switch (shapeOf(id)) {
    case BlockShape::SlabBottom: return detail::kSlabBottomBoxes;
    case BlockShape::SlabTop:    return detail::kSlabTopBoxes;
    case BlockShape::Cube:       break;
    }
    return detail::kCubeBoxes;
}

/// True when the block fills its whole cell, which is what lets the greedy mesher
/// treat it as a merge candidate and what makes `blockBoxes` a single full box.
constexpr bool isFullCube(BlockId id) { return shapeOf(id) == BlockShape::Cube; }

/// Which half a slab goes in, from the face that was clicked and where on it.
///
/// **Vanilla's rule, and it is worth stating because it is not the obvious one.**
/// Clicking the *top* of a block puts the slab in the bottom half of the cell above --
/// so a slab laid on a floor sits on the floor. Clicking the *bottom* puts it in the
/// top half, so one placed on a ceiling hangs from it. A side face is decided by
/// height: the upper half of the face makes a top slab, the lower half a bottom one,
/// which is what lets a player build either without moving.
///
/// `hitY` is the y coordinate of the point the ray met the block, in world units. Only
/// its fractional part matters and only for a side face.
///
/// Free function on the face and a number rather than a method on a hit, so a test can
/// ask it every case without a world to raycast through.
constexpr bool slabGoesInTopHalf(Face face, f32 hitY) {
    if (face == Face::PosY) {
        return false; // Clicked a top surface: rest on it.
    }
    if (face == Face::NegY) {
        return true; // Clicked an underside: hang from it.
    }
    // A side. This is `hitY - floor(hitY)`, spelled out because a cast truncates
    // toward zero rather than downward: -3.25 must give 0.75, not -0.25. Testing
    // against the truncated value rather than against zero is what keeps a hit
    // exactly on a negative boundary -- y = -3.0 -- giving a fraction of 0 instead of
    // a whole block.
    const auto truncated = static_cast<f32>(static_cast<i32>(hitY));
    const f32 cell = hitY < truncated ? truncated - 1.0f : truncated;
    return hitY - cell >= 0.5f;
}

/// Whether putting `held` into the cell already holding `existing` fills it.
///
/// **Vanilla's rule, and it is the difference between slabs you can build with and
/// slabs you can only lay end to end.** Clicking the top of a bottom slab with another
/// slab does not put one in the cell above -- it fills the half that is empty and the
/// two become a full block. Without it a wall means alternating bottom and top slabs
/// by hand, and changing your mind costs a block.
///
/// Both halves have to be the same material, which today means both are oak.
///
/// **The face alone decides it, and reusing `slabGoesInTopHalf` here was wrong.** That
/// function answers "which half does a slab go in when it lands in an *empty* cell",
/// and clicking a top surface answers *bottom* there -- a slab laid on a floor rests on
/// it. Filling a cell that already holds one is the opposite question: the top face of
/// a bottom slab is the face that looks into the empty half, so clicking it fills the
/// *top*. Two rules that read the same and are not.
///
/// A side face never combines, which is also vanilla: the ray can only reach the side
/// of a bottom slab within its own lower half, so there is nothing free behind it and
/// the placement belongs in the cell next door.
constexpr bool combinesIntoDoubleSlab(BlockId held, BlockId existing, Face face) {
    // Plain locals rather than `static constexpr`, which C++20 forbids inside a
    // constexpr function. `blockIdOf` is consteval either way, so the lookup still
    // happens at compile time and a rename is still a compile error.
    constexpr BlockId kBottom = blockIdOf("oak_slab");
    constexpr BlockId kTop = blockIdOf("oak_slab_top");

    if (held != kBottom) {
        return false; // Only a slab in hand can fill a slab's other half.
    }
    return (existing == kBottom && face == Face::PosY)
        || (existing == kTop && face == Face::NegY);
}

/// The block a filled cell becomes. Only meaningful when `combinesIntoDoubleSlab` said
/// yes; kept beside it so the two names cannot drift apart.
inline BlockId doubleSlabFor(BlockId /*held*/) {
    static constexpr BlockId kDouble = blockIdOf("oak_slab_double");
    return kDouble;
}

/// The block id to place, given what the player is holding and where they clicked.
///
/// Returns `held` unchanged for anything that is not a slab, so the placement path
/// calls this once rather than branching on shape.
inline BlockId placedVariant(BlockId held, Face face, f32 hitY) {
    if (shapeOf(held) != BlockShape::SlabBottom) {
        return held;
    }
    if (!slabGoesInTopHalf(face, hitY)) {
        return held;
    }
    // Resolved during constant evaluation, so a rename that missed this line is a
    // compile error rather than a slab that silently stops flipping.
    static constexpr BlockId kTopHalf = blockIdOf("oak_slab_top");
    return kTopHalf;
}

} // namespace mc
