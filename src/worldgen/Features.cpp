#include "worldgen/Features.hpp"

#include "core/Profile.hpp"
#include "world/BlockTable.hpp"
#include "worldgen/FeatureTable.hpp"

#include <algorithm>
#include <cmath>

namespace mc {
namespace {

/// Stateless hash over the whole placement key.
///
/// Stateless rather than a seeded RNG advanced per draw, and that is load bearing:
/// a column has to be able to ask "where did my neighbour's 40th gravel blob go?"
/// without replaying the 39 before it, or the 3x3 replay in `place` would cost nine
/// full generations instead of nine cheap lookups.
u32 hashKey(u32 seed, i32 columnX, i32 columnZ, u32 feature, u32 attempt, u32 stream) {
    u32 h = seed;
    h = (h ^ static_cast<u32>(columnX)) * 0x9E3779B1u;
    h = (h ^ static_cast<u32>(columnZ)) * 0x85EBCA77u;
    h = (h ^ feature) * 0xC2B2AE3Du;
    h = (h ^ attempt) * 0x27D4EB2Fu;
    h = (h ^ stream) * 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}

f32 unitOf(u32 hash) {
    return static_cast<f32>(hash >> 8) / 16777216.0f;
}

/// Inverse CDF of a triangular distribution on [a, b] peaking at c.
i32 sampleTriangle(f32 u, i32 a, i32 b, i32 c) {
    const auto fa = static_cast<f32>(a);
    const auto fb = static_cast<f32>(b);
    const f32 fc = std::clamp(static_cast<f32>(c), fa, fb);

    const f32 width = fb - fa;
    if (width <= 0.0f) {
        return a;
    }

    const f32 split = (fc - fa) / width;
    const f32 value = u < split
                          ? fa + std::sqrt(u * width * (fc - fa))
                          : fb - std::sqrt((1.0f - u) * width * (fb - fc));
    return static_cast<i32>(std::lround(value));
}

i32 sampleHeight(const BlobSpec& spec, u32 hash) {
    const f32 u = unitOf(hash);
    if (spec.distribution == HeightDistribution::Triangle) {
        return sampleTriangle(u, spec.minY, spec.maxY, spec.peakY);
    }
    const i32 span = spec.maxY - spec.minY;
    return spec.minY + static_cast<i32>(u * static_cast<f32>(span + 1));
}

/// Radius of a sphere holding roughly `blocks` voxels.
f32 radiusFor(i32 blocks) {
    // (4/3) pi r^3 = n  ->  r = cbrt(3n / 4pi)
    return std::cbrt(3.0f * static_cast<f32>(std::max(1, blocks)) / (4.0f * 3.14159265f));
}

/// Reads a block from a column, treating anything outside it as solid.
///
/// "Outside is solid" is the deliberate choice for the air-exposure test. The
/// honest answer would need the neighbouring column's voxels, which may not have
/// been generated yet -- and asking for them would put an ordering dependency
/// between columns that the whole streaming design exists to avoid. The error is
/// confined to blobs touching a column border, and it errs toward placing ore
/// rather than toward holes in a vein.
BlockId blockOrSolid(const Chunk& column, i32 localX, i32 worldY, i32 localZ) {
    if (localX < 0 || localX >= kSectionSize || localZ < 0 || localZ >= kSectionSize) {
        return kStoneBlock;
    }
    if (!isValidWorldY(worldY)) {
        return kStoneBlock;
    }
    const Section* section = column.sectionAt(blockToSectionCoord(worldY));
    if (section == nullptr) {
        return kStoneBlock;
    }
    return section->get(localX, blockToLocalCoord(worldY), localZ);
}

bool touchesAir(const Chunk& column, i32 localX, i32 worldY, i32 localZ) {
    return blockOrSolid(column, localX - 1, worldY, localZ) == kAirBlock
        || blockOrSolid(column, localX + 1, worldY, localZ) == kAirBlock
        || blockOrSolid(column, localX, worldY - 1, localZ) == kAirBlock
        || blockOrSolid(column, localX, worldY + 1, localZ) == kAirBlock
        || blockOrSolid(column, localX, worldY, localZ - 1) == kAirBlock
        || blockOrSolid(column, localX, worldY, localZ + 1) == kAirBlock;
}

/// Places one blob, keeping only what falls inside `target`.
void placeBlob(Chunk& target, const BlobSpec& spec, u32 seed, ChunkPos source,
               u32 feature, u32 attempt) {
    const i32 blocks = spec.minBlocks
                     + static_cast<i32>(unitOf(hashKey(seed, source.x, source.z, feature,
                                                       attempt, 3u))
                                        * static_cast<f32>(spec.maxBlocks - spec.minBlocks + 1));
    if (blocks <= 0) {
        return;
    }

    const i32 centreX = source.x * kSectionSize
                      + static_cast<i32>(hashKey(seed, source.x, source.z, feature, attempt, 0u)
                                         % static_cast<u32>(kSectionSize));
    const i32 centreZ = source.z * kSectionSize
                      + static_cast<i32>(hashKey(seed, source.x, source.z, feature, attempt, 1u)
                                         % static_cast<u32>(kSectionSize));
    const i32 centreY = sampleHeight(spec, hashKey(seed, source.x, source.z, feature,
                                                   attempt, 2u));
    if (!isValidWorldY(centreY)) {
        return;
    }

    const f32 radius = radiusFor(blocks);
    const auto span = static_cast<i32>(radius) + 1;

    // Reject before touching voxels when the blob cannot reach this column at all.
    // Eight of every nine replays end here, which is what makes the 3x3 sweep
    // affordable.
    const i32 baseX = target.position().x * kSectionSize;
    const i32 baseZ = target.position().z * kSectionSize;
    if (centreX + span < baseX || centreX - span >= baseX + kSectionSize
        || centreZ + span < baseZ || centreZ - span >= baseZ + kSectionSize) {
        return;
    }

    const f32 radiusSquared = radius * radius;

    for (i32 dy = -span; dy <= span; ++dy) {
        const i32 worldY = centreY + dy;
        if (!isValidWorldY(worldY)) {
            continue;
        }
        Section* section = target.sectionAt(blockToSectionCoord(worldY));
        if (section == nullptr) {
            continue;
        }
        const i32 localY = blockToLocalCoord(worldY);

        for (i32 dz = -span; dz <= span; ++dz) {
            const i32 localZ = centreZ + dz - baseZ;
            if (localZ < 0 || localZ >= kSectionSize) {
                continue;
            }
            for (i32 dx = -span; dx <= span; ++dx) {
                const i32 localX = centreX + dx - baseX;
                if (localX < 0 || localX >= kSectionSize) {
                    continue;
                }

                // Jittered radius, so a vein is lumpy rather than a clean ball.
                // Vanilla gets the same effect from placing along a line segment
                // with a varying radius; this is the cheap version of that shape
                // and the difference is not visible in rock.
                const f32 jitter =
                    0.72f + 0.56f * unitOf(hashKey(seed, centreX + dx, centreZ + dz,
                                                   feature, static_cast<u32>(worldY), 4u));
                const auto distanceSquared =
                    static_cast<f32>(dx * dx + dy * dy + dz * dz);
                if (distanceSquared > radiusSquared * jitter) {
                    continue;
                }

                const BlockId existing = section->get(localX, localY, localZ);
                if (!isStoneLike(existing)) {
                    continue;
                }

                if (spec.airDiscard > 0.0f && touchesAir(target, localX, worldY, localZ)) {
                    const f32 roll = unitOf(hashKey(seed, centreX + dx, centreZ + dz,
                                                    feature, static_cast<u32>(worldY), 5u));
                    if (roll < spec.airDiscard) {
                        continue;
                    }
                }

                section->set(localX, localY, localZ,
                             existing == kDeepslateBlock ? spec.inDeepslate : spec.inStone);
            }
        }
    }
}

/// World Y of the topmost non-air block in a column of the chunk, or kWorldMinY - 1
/// when there is nothing.
i32 surfaceOf(const Chunk& column, i32 localX, i32 localZ) {
    for (i32 y = kWorldMaxY - 1; y >= kWorldMinY; --y) {
        const Section* section = column.sectionAt(blockToSectionCoord(y));
        if (section == nullptr) {
            continue;
        }
        if (section->isEmpty()) {
            // Skip the whole section rather than 32 reads of a known answer. Most of
            // a column above the surface is exactly this.
            y = sectionIndexToWorldY(sectionIndexInColumn(blockToSectionCoord(y)));
            continue;
        }
        if (section->get(localX, blockToLocalCoord(y), localZ) != kAirBlock) {
            return y;
        }
    }
    return kWorldMinY - 1;
}

void setLocal(Chunk& column, i32 localX, i32 worldY, i32 localZ, BlockId block) {
    Section* section = column.sectionAt(blockToSectionCoord(worldY));
    if (section != nullptr) {
        section->set(localX, blockToLocalCoord(worldY), localZ, block);
    }
}

/// Plants one tree, entirely inside `column`.
///
/// No 3x3 replay and no cross-column writes: see the note on TreeSpec for why trees
/// are the one feature that cannot be seamless, and what the band along each edge
/// costs.
void placeTree(Chunk& column, const TreeSpec& spec, u32 seed, u32 feature, u32 attempt) {
    const ChunkPos pos = column.position();
    const i32 radius = spec.canopyRadius;

    // The plantable window, inset so the canopy cannot reach the column wall.
    const i32 span = kSectionSize - 2 * radius;
    if (span <= 0) {
        return;
    }

    const i32 localX = radius
                     + static_cast<i32>(hashKey(seed, pos.x, pos.z, feature, attempt, 0u)
                                        % static_cast<u32>(span));
    const i32 localZ = radius
                     + static_cast<i32>(hashKey(seed, pos.x, pos.z, feature, attempt, 1u)
                                        % static_cast<u32>(span));

    // Trees grow on grass and nothing else. That one test also keeps them out of
    // caves for free: the surface pass grasses the top of the terrain and explicitly
    // not cave ceilings, so a cave roof is stone and fails here.
    const i32 groundY = surfaceOf(column, localX, localZ);
    if (!isValidWorldY(groundY)) {
        return;
    }
    const Section* groundSection = column.sectionAt(blockToSectionCoord(groundY));
    if (groundSection == nullptr
        || groundSection->get(localX, blockToLocalCoord(groundY), localZ) != kGrassBlock) {
        return;
    }

    const i32 height = spec.minHeight
                     + static_cast<i32>(hashKey(seed, pos.x, pos.z, feature, attempt, 2u)
                                        % static_cast<u32>(spec.maxHeight - spec.minHeight + 1));
    const i32 base = groundY + 1;
    if (!isValidWorldY(base + height + 1)) {
        return;
    }

    // Leaves first, logs second, so the trunk overwrites the leaf column rather than
    // the other way round.
    //
    // Vanilla oak's canopy is two wide layers then two narrow ones, with the corners
    // of the wide layers knocked out at random. That corner rule is most of why a
    // canopy reads as foliage instead of as a cube.
    for (i32 layer = 0; layer < 5; ++layer) {
        const i32 leafY = base + height - 3 + layer;
        if (!isValidWorldY(leafY)) {
            continue;
        }
        const i32 leafRadius = layer <= 1 ? radius : 1;

        for (i32 dz = -leafRadius; dz <= leafRadius; ++dz) {
            for (i32 dx = -leafRadius; dx <= leafRadius; ++dx) {
                const bool corner = std::abs(dx) == leafRadius && std::abs(dz) == leafRadius;
                if (corner) {
                    // Always gone from the topmost layer, a coin flip below it.
                    if (layer == 4) {
                        continue;
                    }
                    if (unitOf(hashKey(seed, pos.x * kSectionSize + localX + dx,
                                       pos.z * kSectionSize + localZ + dz, feature,
                                       static_cast<u32>(leafY), 3u)) < 0.5f) {
                        continue;
                    }
                }

                const i32 x = localX + dx;
                const i32 z = localZ + dz;
                Section* section = column.sectionAt(blockToSectionCoord(leafY));
                if (section == nullptr) {
                    continue;
                }
                if (section->get(x, blockToLocalCoord(leafY), z) != kAirBlock) {
                    continue; // Never carve into terrain to make room.
                }
                section->set(x, blockToLocalCoord(leafY), z, spec.leaves);
            }
        }
    }

    for (i32 dy = 0; dy < height; ++dy) {
        setLocal(column, localX, base + dy, localZ, spec.log);
    }
}

} // namespace

void FeaturePlacer::placeTrees(Chunk& chunk) const {
    const ChunkPos pos = chunk.position();

    for (u32 feature = 0; feature < kTreeFeatures.size(); ++feature) {
        const TreeSpec& spec = kTreeFeatures[feature];

        // Offset the feature index past the blob features so a tree and a blob with
        // the same index cannot share a hash stream and place in lockstep.
        const auto key = static_cast<u32>(kBlobFeatures.size()) + feature;

        const auto whole = static_cast<u32>(spec.triesPerColumn);
        for (u32 attempt = 0; attempt < whole; ++attempt) {
            placeTree(chunk, spec, m_seed, key, attempt);
        }

        const f32 fraction = spec.triesPerColumn - static_cast<f32>(whole);
        if (fraction > 0.0f
            && unitOf(hashKey(m_seed, pos.x, pos.z, key, whole, 6u)) < fraction) {
            placeTree(chunk, spec, m_seed, key, whole);
        }
    }
}

void FeaturePlacer::placeFrom(Chunk& target, ChunkPos source) const {
    for (u32 feature = 0; feature < kBlobFeatures.size(); ++feature) {
        const BlobSpec& spec = kBlobFeatures[feature];

        const auto whole = static_cast<u32>(spec.triesPerColumn);
        for (u32 attempt = 0; attempt < whole; ++attempt) {
            placeBlob(target, spec, m_seed, source, feature, attempt);
        }

        // The fractional remainder, for batches rarer than once per column.
        const f32 fraction = spec.triesPerColumn - static_cast<f32>(whole);
        if (fraction > 0.0f
            && unitOf(hashKey(m_seed, source.x, source.z, feature, whole, 6u)) < fraction) {
            placeBlob(target, spec, m_seed, source, feature, whole);
        }
    }
}

void FeaturePlacer::place(Chunk& chunk) const {
    MC_PROFILE_SCOPE_N("FeaturePlacer::place");

    const ChunkPos centre = chunk.position();
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            placeFrom(chunk, ChunkPos{centre.x + dx, centre.z + dz});
        }
    }

    // After the blobs, so a tree stands on whatever rock the blobs left and not the
    // other way round -- and because a tree needs the surface pass to have grassed
    // the ground it tests for.
    placeTrees(chunk);
}

} // namespace mc
