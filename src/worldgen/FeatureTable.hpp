#pragma once

#include "core/Types.hpp"
#include "world/BlockTable.hpp"

#include <array>
#include <string_view>

namespace mc {

/// How a blob's height is drawn.
enum class HeightDistribution : u8 {
    /// Equally likely anywhere in the range.
    Uniform,
    /// Peaks at `peakY` and falls off linearly to both ends. Vanilla calls this
    /// trapezoid; with the plateau width left at zero the two are the same shape.
    Triangle,
};

/// One blob-placing feature.
///
/// Vanilla splits this in two -- a *configured* feature holding size and the air
/// rule, a *placed* feature holding the count and the height provider. There is no
/// reason to keep them apart here, because nothing reuses one half with a different
/// other half.
struct BlobSpec {
    std::string_view name;

    /// The block placed in stone, and the one placed in deepslate. Equal when the
    /// block has no deep variant; ores all have one, stone variants do not.
    BlockId inStone = kAirBlock;
    BlockId inDeepslate = kAirBlock;

    /// Attempts per **column**, already multiplied by 4 from vanilla's per-chunk
    /// figure -- a Minecraft chunk is 16x16 and a column here is 32x32. Fractional
    /// because one vanilla batch is rarer than once per chunk.
    f32 triesPerColumn = 0.0f;

    /// Blocks per blob, not vanilla's `size` parameter. The two are not the same
    /// number and confusing them is the trap RESEARCH.md 3 warns about.
    i32 minBlocks = 1;
    i32 maxBlocks = 1;

    i32 minY = kWorldMinY;
    i32 maxY = kWorldMaxY - 1;
    /// Only read for Triangle.
    i32 peakY = 0;
    HeightDistribution distribution = HeightDistribution::Uniform;

    /// Chance that a block touching air is dropped. 1.0 means the ore never shows
    /// on a cave wall at all. This is the rule that needs carvers to have run
    /// first, which is why features are the last stage.
    f32 airDiscard = 0.0f;
};

/// Every blob feature, in the order they run. Numbers are RESEARCH.md 3, which
/// carries the sources and the caveats.
///
/// **Order matters between the two groups.** Stone variants go first so that ores
/// can then replace them, which is what vanilla does -- an ore's target list names
/// granite, diorite, andesite and tuff alongside stone. Running ores first would
/// let a variant blob erase ore that had already been placed.
///
/// Emerald is absent on purpose. It generates only in mountain biomes, and there
/// are no biomes yet; shipping it biome-blind would put emerald everywhere, which
/// is worse than not having it. Badlands gold is missing for the same reason. Both
/// wait for 4d -- see RESEARCH.md 3.
inline constexpr std::array kBlobFeatures{
    // -- stone variants and gravel -------------------------------------------
    //
    // These four counts are the one place in this table not taken from a
    // confirmed Java figure: the wiki page carries Bedrock Edition values only
    // (RESEARCH.md 6). They are chosen to read right in a cross-section rather
    // than copied, and are the first thing to revisit if the underground looks
    // too busy or too plain.
    BlobSpec{"granite",  blockIdOf("granite"),  blockIdOf("granite"),
             8.0f, 12, 33, 0, 80, 0, HeightDistribution::Uniform, 0.0f},
    BlobSpec{"diorite",  blockIdOf("diorite"),  blockIdOf("diorite"),
             8.0f, 12, 33, 0, 80, 0, HeightDistribution::Uniform, 0.0f},
    BlobSpec{"andesite", blockIdOf("andesite"), blockIdOf("andesite"),
             8.0f, 12, 33, 0, 80, 0, HeightDistribution::Uniform, 0.0f},
    // Tuff is the deep one: it belongs to the deepslate layers, not the stone ones.
    BlobSpec{"tuff",     blockIdOf("tuff"),     blockIdOf("tuff"),
             8.0f, 12, 33, kWorldMinY, 0, 0, HeightDistribution::Uniform, 0.0f},
    // Gravel is everywhere and in big blobs, which is what makes it read as
    // sediment rather than as another speckle.
    BlobSpec{"gravel",   blockIdOf("gravel"),   blockIdOf("gravel"),
             56.0f, 16, 64, kWorldMinY, 120, 0, HeightDistribution::Uniform, 0.0f},

    // -- ores -----------------------------------------------------------------
    BlobSpec{"coal_upper", blockIdOf("coal_ore"), blockIdOf("deepslate_coal_ore"),
             120.0f, 0, 37, 136, 320, 0, HeightDistribution::Uniform, 0.0f},
    BlobSpec{"coal", blockIdOf("coal_ore"), blockIdOf("deepslate_coal_ore"),
             80.0f, 0, 37, 0, 192, 96, HeightDistribution::Triangle, 0.5f},

    BlobSpec{"iron_upper", blockIdOf("iron_ore"), blockIdOf("deepslate_iron_ore"),
             360.0f, 0, 13, 80, 320, 232, HeightDistribution::Triangle, 0.0f},
    BlobSpec{"iron_middle", blockIdOf("iron_ore"), blockIdOf("deepslate_iron_ore"),
             40.0f, 0, 13, -24, 56, 16, HeightDistribution::Triangle, 0.0f},
    BlobSpec{"iron_small", blockIdOf("iron_ore"), blockIdOf("deepslate_iron_ore"),
             40.0f, 0, 5, kWorldMinY, 72, 0, HeightDistribution::Uniform, 0.0f},

    BlobSpec{"copper", blockIdOf("copper_ore"), blockIdOf("deepslate_copper_ore"),
             64.0f, 0, 16, -16, 112, 48, HeightDistribution::Triangle, 0.0f},

    // -- the rare ores, calibrated rather than copied ------------------------
    //
    // The four below carry a `x X` note: their attempt counts are the RESEARCH.md 3
    // figure scaled down by that factor, and the factor was measured rather than
    // guessed.
    //
    // Taken literally, the wiki's per-chunk blob counts overshoot badly for exactly
    // the ores that sit deep: the first run of this table produced 42 diamond, 24
    // lapis and 26 gold per 16x16 chunk against vanilla's rough 4, 4 and 7. The
    // reason is that vanilla spreads those attempts over a much wider Y range than
    // the ore's *useful* band -- diamond is uniform over -80..80 -- so a large share
    // of them land in open air above the terrain and place nothing. Reproducing the
    // count without reproducing that waste places every attempt in solid rock.
    //
    // Rather than model the waste, the counts are calibrated against measured
    // density, which is the same discipline caves were tuned with and for the same
    // reason: none of this is visible from any camera position, so a number is the
    // only evidence available. Coal, iron and copper needed no correction -- they
    // landed at 137, 78 and 109 per chunk against vanilla's rough 130, 80 and 100 --
    // which is the check that the scaling itself is right and only the deep bands
    // were wrong.
    BlobSpec{"gold", blockIdOf("gold_ore"), blockIdOf("deepslate_gold_ore"), // x0.27
             4.3f, 0, 13, kWorldMinY, 32, -16, HeightDistribution::Triangle, 0.5f},
    BlobSpec{"gold_deep", blockIdOf("gold_ore"), blockIdOf("deepslate_gold_ore"),
             0.55f, 0, 13, kWorldMinY, -48, 0, HeightDistribution::Uniform, 0.5f},

    // Redstone is the one ore with no air rule at all, so it is the one you
    // actually see in a cave wall.
    BlobSpec{"redstone", blockIdOf("redstone_ore"), blockIdOf("deepslate_redstone_ore"), // x0.52
             8.3f, 0, 10, kWorldMinY, 15, 0, HeightDistribution::Uniform, 0.0f},
    BlobSpec{"redstone_deep", blockIdOf("redstone_ore"), blockIdOf("deepslate_redstone_ore"),
             16.6f, 0, 10, -63, -32, -63, HeightDistribution::Triangle, 0.0f},

    BlobSpec{"lapis", blockIdOf("lapis_ore"), blockIdOf("deepslate_lapis_ore"), // x0.17
             1.4f, 0, 10, -32, 32, 0, HeightDistribution::Triangle, 0.0f},
    // Buried: never exposed at all.
    BlobSpec{"lapis_buried", blockIdOf("lapis_ore"), blockIdOf("deepslate_lapis_ore"),
             2.7f, 0, 10, kWorldMinY, 64, 0, HeightDistribution::Uniform, 1.0f},

    BlobSpec{"diamond", blockIdOf("diamond_ore"), blockIdOf("deepslate_diamond_ore"), // x0.10
             2.8f, 1, 5, -63, 16, -59, HeightDistribution::Triangle, 0.5f},
    BlobSpec{"diamond_large", blockIdOf("diamond_ore"), blockIdOf("deepslate_diamond_ore"),
             0.044f, 1, 23, -63, 16, -59, HeightDistribution::Triangle, 0.7f},
    BlobSpec{"diamond_buried", blockIdOf("diamond_ore"), blockIdOf("deepslate_diamond_ore"),
             1.6f, 1, 10, -63, 16, -59, HeightDistribution::Triangle, 1.0f},
    BlobSpec{"diamond_deep", blockIdOf("diamond_ore"), blockIdOf("deepslate_diamond_ore"),
             0.8f, 1, 10, -63, -4, -59, HeightDistribution::Triangle, 0.5f},
};

/// A tree.
///
/// **Not a `BlobSpec`, and the difference is not the shape.** A blob's geometry is a
/// pure function of (seed, column, feature, attempt), so a column can work out where
/// its neighbour's blobs went without asking. A tree stands *on the ground*, so its
/// height depends on the terrain at its trunk -- and when column T replays column S's
/// trees, T has no way to know how high S's ground is. That is a world read across a
/// border, which is exactly the ordering dependency the streaming design refuses.
///
/// So trees do not cross columns at all: a trunk must sit far enough from the edge
/// that its whole canopy fits inside its own column. **The cost is a band of
/// `canopyRadius` blocks along each column edge where no tree ever grows**, which at
/// radius 2 in a 32-wide column leaves 28x28 of it plantable. It is a real artefact
/// -- there is a faint grid of tree-free strips -- and it is the same kind of
/// deliberate compromise as the air-exposure rule treating outside the column as
/// solid. The alternative is a chunk-status pipeline like vanilla's, which is a much
/// larger thing than trees.
struct TreeSpec {
    std::string_view name;

    BlockId log = kAirBlock;
    BlockId leaves = kAirBlock;

    /// Attempts per column. Not every attempt plants: most land on stone, sand or a
    /// slope and are rejected.
    f32 triesPerColumn = 0.0f;

    /// Trunk height in logs. Vanilla oak is 4 to 6.
    i32 minHeight = 4;
    i32 maxHeight = 6;

    /// Horizontal reach of the widest leaf layer, and therefore the width of the
    /// no-tree band along each column edge.
    i32 canopyRadius = 2;
};

/// Trees, such as there are. One species, because there are no biomes to tell a
/// birch forest from an oak one -- see the note on emerald above, which is the same
/// argument.
///
/// Five attempts per 32x32 column is roughly one per vanilla 16x16 chunk before
/// rejections, which lands between vanilla's plains and its sparse forest. Without
/// biomes the density has to be one number everywhere, so it is deliberately on the
/// thin side: uniform thick forest reads worse than uniform light woodland.
inline constexpr std::array kTreeFeatures{
    TreeSpec{"oak", blockIdOf("oak_log"), blockIdOf("oak_leaves"), 5.0f, 4, 6, 2},
};

} // namespace mc
