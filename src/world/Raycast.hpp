#pragma once

#include "core/Math.hpp"
#include "world/Coords.hpp"

#include <optional>

namespace mc {

class World;

/// What a ray found, and everything a caller needs to act on it.
struct RaycastHit {
    /// The solid voxel the ray entered. This is what breaking removes.
    BlockPos block{};

    /// The empty voxel the ray was in immediately before, i.e. `block` stepped
    /// back along the face it was entered through.
    ///
    /// Carried rather than recomputed because the caller would have to rebuild it
    /// from `face` anyway, and getting that offset backwards puts placed blocks
    /// inside the one that was clicked. This is where placement goes.
    BlockPos adjacent{};

    /// Which face of `block` the ray crossed to get in.
    Face face = Face::PosY;

    /// Distance along the ray, in blocks.
    f32 distance = 0.0f;
};

/// Marches the voxel grid from `origin` along `direction` and returns the first
/// non-air block within `maxDistance`.
///
/// Amanatides & Woo: step one grid cell at a time and always cross the nearest
/// cell boundary next, so every voxel the ray touches is visited exactly once and
/// none is skipped. A fixed-step sampler is the obvious alternative and is wrong
/// twice over -- too coarse and it tunnels through a one-block wall, too fine and
/// it samples the same voxel dozens of times.
///
/// `direction` need not be normalized; it is normalized here, so `distance` and
/// `maxDistance` are always in blocks.
///
/// Columns that are unloaded *or still generating* read as air (`World::blockAt`),
/// so a ray leaving the loaded region finds nothing rather than reporting a block
/// that was never generated -- or racing a worker that is mid-way through writing
/// one.
std::optional<RaycastHit> raycast(const World& world, vec3 origin, vec3 direction,
                                  f32 maxDistance);

/// Offsets a block position one step along a face's outward normal.
constexpr BlockPos offsetByFace(BlockPos pos, Face face) {
    switch (face) {
        case Face::NegX: return {pos.x - 1, pos.y, pos.z};
        case Face::PosX: return {pos.x + 1, pos.y, pos.z};
        case Face::NegY: return {pos.x, pos.y - 1, pos.z};
        case Face::PosY: return {pos.x, pos.y + 1, pos.z};
        case Face::NegZ: return {pos.x, pos.y, pos.z - 1};
        case Face::PosZ: return {pos.x, pos.y, pos.z + 1};
    }
    return pos;
}

} // namespace mc
