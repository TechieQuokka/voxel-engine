#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"

#include <span>
#include <vector>

namespace mc {

/// One face of a held item's model, in the item's own space.
///
/// The same shape as the character's `GpuQuad` -- a corner and two full-length edges
/// -- because that is the buffer it is drawn from. `cross(uAxis, vAxis)` points out
/// of the model, which is the winding rule every other quad in this engine follows.
struct ItemQuad {
    vec3 origin{};
    vec3 uAxis{};
    vec3 vAxis{};
    /// Linear rgb. Used only when `layer` is `kFlatColour`.
    vec3 color{};
    /// Texture array layer, or `kFlatColour` for a face that carries its own colour.
    f32 layer = 0.0f;

    /// Whether the sprite is sampled right-to-left across this face.
    ///
    /// **Only the back face sets it, and without it the model is inconsistent with
    /// itself.** A quad's texture coordinate comes from its corner, so a face wound
    /// to point backwards samples column 0 at the +X end -- the image ends up
    /// mirrored *in the model's own space* while the rim, which is built from the
    /// sprite's real pixel positions, is not. Seen from behind, the drawn tool and
    /// its edges then disagree by a reflection: they cross in an X.
    ///
    /// Mirroring the sampling puts column 0 back at the -X end on both faces, which
    /// is what a real object does -- and the image *looks* mirrored from behind
    /// because you are behind it, not because the texture was flipped.
    bool mirrorU = false;

    static constexpr f32 kFlatColour = -1.0f;
};

/// How thick a held item is, in blocks.
///
/// **One pixel, which is what vanilla extrudes an item sprite to.** A held tool is
/// not a flat billboard in Minecraft: it has a visible edge, and during the swing
/// that edge is what stops the tool disappearing as it turns through the view.
inline constexpr f32 kItemThickness = 1.0f / 16.0f;

/// Builds the model a 16x16 icon becomes when it is held: the sprite, extruded.
///
/// `pixels` is RGBA, row-major, **top row first**, `size` on a side. `layer` is where
/// that sprite lives in the block texture array, so the two flat faces can be drawn
/// from the texture rather than from a colour per pixel.
///
/// The result is centred on the origin and one block across, so the caller scales it
/// by whatever the display transform asks for. Its front faces +Z, its top is +Y.
///
/// **The two flat faces are textured and the rim is not**, which is not an
/// inconsistency: the flat faces are the whole sprite and an alpha discard gives them
/// their silhouette for free, while a rim face is one texel seen edge-on and has no
/// sensible texture coordinate at all. It takes the colour of the pixel it belongs
/// to, which is what vanilla's own extrusion does.
std::vector<ItemQuad> buildSpriteModel(std::span<const u8> pixels, u32 size, f32 layer);

/// Builds the cube a held *block* is -- vanilla holds a block as a block, not as a
/// picture of one. Centred on the origin and one block across, like the sprite model.
std::vector<ItemQuad> buildBlockModel(f32 topLayer, f32 sideLayer, f32 bottomLayer);

} // namespace mc
