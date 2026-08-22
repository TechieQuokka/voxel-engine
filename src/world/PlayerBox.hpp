#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <cmath>

namespace mc {

/// The player's collision box: where they stand, how tall they are, and how wide.
///
/// **These numbers lived in `CharacterRenderer` while the camera was the only thing
/// that asked for them, and the cost of that has now been paid twice.** The first
/// time was item pickup, which shipped broken for four play sessions because no test
/// could reach `kPickupRadius` and `kEyeHeight` at once -- `tests/CMakeLists.txt`
/// still names that case. The second was block placement, whose "is this inside the
/// player" test was written from the numbers its caller happened to have: a two-block
/// column at the feet with **no width at all**, which let a player seal a block into
/// their own shoulder.
///
/// They are in `world` now because physics is what asks these questions, and because
/// a rule this engine already wrote down applies directly: a constant only the caller
/// can see cannot be tested. The renderer takes its model height from here rather
/// than the other way round, so what is drawn and what collides cannot drift.
struct PlayerBox {
    /// Eye height, matching Minecraft's 1.62. The camera sits here above the feet.
    static constexpr f32 kEyeHeight = 1.62f;

    /// Feet to the top of the head. Vanilla's player is 1.8; this engine draws and
    /// collides a two-block character, and the two agreeing matters more than either
    /// matching vanilla exactly.
    static constexpr f32 kHeight = 2.0f;

    /// Vanilla's player is 0.6 wide, centred on the feet position -- so the box is
    /// narrower than the block it stands on, which is what lets a player walk into a
    /// one-block gap.
    static constexpr f32 kWidth = 0.6f;
    static constexpr f32 kHalfWidth = kWidth * 0.5f;

    /// Where the feet are. Everything else is derived, so there is one position to
    /// pass and no way to pass the eye by mistake -- which is the whole of the pickup
    /// bug `playerFeet()` exists to prevent.
    vec3 feet{};

    /// The inclusive range of block cells this box overlaps.
    ///
    /// **Returned as a range rather than tested against a world here**, so that
    /// `world/PlayerBox.hpp` needs no `World` and a test can check the arithmetic
    /// without building terrain. The caller walks the range asking whatever question
    /// it has -- solid, fluid, or something Phase 18 has not invented yet.
    struct CellRange {
        i32 minX = 0;
        i32 minY = 0;
        i32 minZ = 0;
        i32 maxX = 0;
        i32 maxY = 0;
        i32 maxZ = 0;
    };

    CellRange cells() const noexcept {
        return CellRange{spanMin(feet.x - kHalfWidth), spanMin(feet.y),
                         spanMin(feet.z - kHalfWidth), spanMax(feet.x + kHalfWidth),
                         spanMax(feet.y + kHeight),    spanMax(feet.z + kHalfWidth)};
    }

    /// Whether the unit cube at `pos` overlaps this box.
    ///
    /// **Touching is not overlapping, and that asymmetry is load bearing.** The block
    /// the player is standing on has its top face exactly at `feet.y`; if that counted
    /// as an overlap, the floor under your own feet would be unbuildable and a player
    /// could not bridge outward from where they stand. The same goes for the four
    /// blocks the box exactly abuts when standing on a grid line.
    bool intersects(BlockPos pos) const noexcept {
        const auto x = static_cast<f32>(pos.x);
        const auto y = static_cast<f32>(pos.y);
        const auto z = static_cast<f32>(pos.z);

        return x + 1.0f > feet.x - kHalfWidth && x < feet.x + kHalfWidth
            && y + 1.0f > feet.y && y < feet.y + kHeight
            && z + 1.0f > feet.z - kHalfWidth && z < feet.z + kHalfWidth;
    }

    /// Whether a sub-cell box inside the block at `pos` overlaps this box.
    ///
    /// **The same rule as `intersects` above, with the cube's 0..1 replaced by the
    /// box's own bounds** -- and it is spelled as a separate overload rather than the
    /// unit-cube version calling it, so that the cube path keeps exactly the arithmetic
    /// it has always had. A slab that collides half a pixel differently from the block
    /// it replaced would be felt on the first step and blamed on the slab.
    ///
    /// Takes the bounds already converted to fractions of a block, so this header
    /// stays free of `BlockShape` and a test can pass numbers directly.
    bool intersectsBox(BlockPos pos, f32 minX, f32 minY, f32 minZ, f32 maxX, f32 maxY,
                       f32 maxZ) const noexcept {
        const auto x = static_cast<f32>(pos.x);
        const auto y = static_cast<f32>(pos.y);
        const auto z = static_cast<f32>(pos.z);

        return x + maxX > feet.x - kHalfWidth && x + minX < feet.x + kHalfWidth
            && y + maxY > feet.y && y + minY < feet.y + kHeight
            && z + maxZ > feet.z - kHalfWidth && z + minZ < feet.z + kHalfWidth;
    }

private:
    /// First cell a span starting at `low` touches.
    static i32 spanMin(f32 low) noexcept {
        return static_cast<i32>(std::floor(low));
    }

    /// Last cell a span ending at `high` touches. **Touching is not overlapping**, so
    /// a span that ends exactly on a boundary stops at the cell below it -- the same
    /// rule `intersects` applies, spelled once so the two cannot disagree.
    static i32 spanMax(f32 high) noexcept {
        return static_cast<i32>(std::ceil(high)) - 1;
    }
};

} // namespace mc
