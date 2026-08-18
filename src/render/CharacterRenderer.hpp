#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/BlockTextures.hpp"
#include "render/Camera.hpp"
#include "render/ItemModel.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/FrameRing.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"
#include "world/ItemTable.hpp"
#include "world/PlayerBox.hpp"

#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mc {

/// Draws a blocky humanoid at a world position.
///
/// **This is the engine's second render path, and the first thing in it that is not
/// voxels.** A chunk `Quad` is six-bit integers on the block lattice; a player is
/// 0.5 blocks wide and its limbs swing, so none of that packing applies. What does
/// carry over is the shape of the solution: no vertex buffer, quads in an SSBO,
/// six corners expanded from `gl_VertexID`, one draw call. The character shader is
/// chunk.vert with floating-point edges substituted for the packed lattice, and it
/// shares chunk.frag's face-brightness table exactly so the model sits in the same
/// light as the ground under it.
///
/// The model is nine boxes in the proportions Minecraft uses -- an 8x8x8 head, an
/// 8x12x4 body, 4x12x4 limbs, at 16 units to the block. Arms are split at the
/// sleeve so the shirt can end partway down, and the hair is a shell over the top
/// of the head rather than a face colour, so it shows from behind. Both of those
/// are most of what makes the silhouette read as a person rather than a stack of
/// crates.
class CharacterRenderer {
public:
    CharacterRenderer();

    /// `facing` is the direction the character looks along, and needs no particular
    /// convention: the model is built from that vector and world up, so whatever
    /// the camera calls forward is what the character faces.
    ///
    /// `walkPhase` advances with distance travelled, not with time, so the limbs
    /// stop when the character does instead of marching on the spot. `walkAmount`
    /// in [0, 1] fades the swing in and out.
    /// `swingPhase` advances with time while mining and `swingAmount` in [0, 1]
    /// fades the chop in and out. The right arm follows the swing instead of the
    /// walk cycle while it is up; every other limb keeps walking, which is what
    /// mining while moving looks like.
    /// `heldItem` is what is in the hotbar slot the player has selected, or
    /// `kNoItem` for an empty hand. It is drawn in the right fist, swinging with the
    /// arm that holds it -- vanilla's tool is not a decal on the model, it is a
    /// separate model in the hand, and it moves because the hand does.
    void draw(rhi::Device& device, const Camera& camera, const vec3& feetPosition,
              const vec3& facing, f32 walkPhase, f32 walkAmount,
              f32 swingPhase, f32 swingAmount, ItemId heldItem,
              const BlockTextures& textures, rhi::FrameRing& ring);

    /// The first-person arm, at the bottom right of the view.
    ///
    /// **Built in world space against the camera basis, not in a separate view-space
    /// projection.** Placing it a fixed offset from the eye and drawing it with the
    /// ordinary view-projection means it reuses this class's shader, quad format and
    /// buffer exactly, and needs no second projection matrix to keep in step with
    /// the first. What it does need is a depth clear first, or the arm intersects
    /// any wall the player stands against -- which is what `Device::clearDepth` is
    /// for and what Minecraft does too.
    /// **Its own slice of the frame ring, not the one `draw` just used.** The two
    /// are never both drawn in a frame today, but they were sharing one buffer at
    /// offset zero, and a view model that overwrote the character mid-frame is
    /// exactly the class of bug the ring exists to remove.
    void drawHand(rhi::Device& device, const Camera& camera, f32 swingPhase,
                  f32 swingAmount, ItemId heldItem, const BlockTextures& textures,
                  rhi::FrameRing& ring);

    /// Height of the model in blocks, from feet to the top of the head, and the eye
    /// height the camera uses to sit in the head rather than at the feet.
    ///
    /// **Both come from `PlayerBox` rather than being declared here.** They used to be
    /// declared here, which put the player's dimensions in the renderer and left
    /// physics reading them out of it -- and the collision box that grew from that had
    /// a height and no width. The model is drawn to match what collides, not the other
    /// way round, so these are aliases and cannot drift from the box.
    static constexpr f32 kHeight = PlayerBox::kHeight;
    static constexpr f32 kEyeHeight = PlayerBox::kEyeHeight;

private:
    /// One quad, laid out exactly as character.vert reads it.
    struct GpuQuad {
        vec4 origin;
        vec4 uAxis;
        vec4 vAxis;
        vec4 color;
    };

    /// One box of the model, in model space: x right, y up, z forward.
    struct Box {
        vec3 min;
        vec3 max;
        /// Point the box rotates about when it swings. Shoulders and hips.
        vec3 pivot;
        /// 0 for the head and body; +1 and -1 for limbs, so left and right swing
        /// in opposition.
        f32 swing;
        /// True for the two boxes of the right arm, which is the one that mines.
        /// A flag rather than an index comparison so reordering the model cannot
        /// silently animate an elbow.
        bool rightArm;
        /// sRGB, decoded once at construction.
        u32 sideArgb;
        u32 topArgb;
    };

    /// Vanilla's display transform for a held item, straight out of the model JSON.
    ///
    /// **Rotation is in degrees and applied X, then Y, then Z; translation is in
    /// sixteenths of a block and applied before the rotation.** Those two sentences
    /// are the whole of Minecraft's `display` convention and getting either backwards
    /// puts the tool through the character's chest.
    struct HeldTransform {
        vec3 rotationDegrees{};
        vec3 translationTexels{};
        f32 scale = 1.0f;
    };

    /// `item/handheld.json`'s translation and scale, and its tilt **turned half way
    /// round**: `125` rather than `55`.
    ///
    /// **Vanilla's number holds the tool by its head.** The icon is drawn with the
    /// head at the top of the tile and the handle running to the bottom-left, so a
    /// tilt that lands the top of the sprite at the fist puts the head in the hand
    /// and leaves the handle swinging below it. Two play sessions said so, the second
    /// one in as many words.
    ///
    /// The half turn is in the sprite's own plane, so it swaps the ends and nothing
    /// else: handle in the fist, head hanging down and forward, which is how a tool
    /// is carried. The remaining sign, the 90 about Y, decides which way the head
    /// leans; vanilla's right-hand form is -90 and this frame wants the other one.
    ///
    /// **That vanilla's own numbers need a half turn here is a symptom, not a
    /// solution.** It says this engine's hand frame differs from Minecraft's by more
    /// than the half turn about X that RESEARCH.md 9.3 accounts for -- most likely
    /// because vanilla's arm is rigged and rotated where this one hangs straight
    /// down. Worth revisiting if the model ever grows a wrist.
    static constexpr HeldTransform kHandheldThirdPerson{
        vec3{0.0f, 90.0f, 125.0f}, vec3{0.0f, 4.0f, 0.5f}, 0.85f};
    /// First person keeps vanilla's **scale** and nothing else of its transform.
    ///
    /// Vanilla's `[0, -90, 25]` and `[1.13, 3.2, 1.13]` describe the frame of a
    /// rigged arm with a wrist, and this engine's view model is one tilted box --
    /// there is no such frame for them to mean anything in. The orientation is the
    /// basis the call site builds instead, and the reasoning is written there and in
    /// DESIGN.md 7.22. A scale needs no frame, so 0.68 is still vanilla's.
    static constexpr HeldTransform kHandheldFirstPerson{
        vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}, 0.68f};

    /// `block/block.json` — a held block is a block, at a bit over a third size.
    /// The 45 degrees about Y is what shows two faces instead of one, and is the
    /// reason a held block reads as a cube rather than as a tile.
    static constexpr HeldTransform kBlockThirdPerson{
        vec3{0.0f, 45.0f, 0.0f}, vec3{0.0f, 2.5f, 0.0f}, 0.375f};
    static constexpr HeldTransform kBlockFirstPerson{
        vec3{0.0f, 45.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}, 0.4f};

    static constexpr usize kBoxCount = 9;
    static constexpr usize kQuadsPerBox = 6;
    static constexpr usize kQuadCount = kBoxCount * kQuadsPerBox;
    static constexpr u32 kVerticesPerQuad = 6;
    static constexpr u32 kQuadBufferBinding = 0;
    /// The block texture array, shared with everything else that draws an icon.
    static constexpr u32 kTextureUnit = 0;

    void appendBox(const Box& box, f32 swingAngle, const vec3& feetPosition,
                   const vec3& right, const vec3& up, const vec3& forward);

    /// The model for one item, built once and kept. Building it means walking 256
    /// pixels and emitting the silhouette, which is cheap -- but not per frame, and
    /// not once per frame *per view* either.
    const std::vector<ItemQuad>& modelFor(ItemId item, const BlockTextures& textures);

    /// Lays a held item's quads into `m_quads`.
    ///
    /// `anchor` is where the fist is and `right`/`up`/`forward` are the frame it is
    /// held in -- the character's basis in third person, the camera's in first. The
    /// display transform is applied inside that frame, so one function serves both.
    void appendHeldItem(const std::vector<ItemQuad>& model, const HeldTransform& display,
                        const vec3& anchor, const vec3& right, const vec3& up,
                        const vec3& forward);

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;

    std::array<Box, kBoxCount> m_boxes;
    std::vector<GpuQuad> m_quads;

    /// One entry per item ever held. A dozen at most in a session, and each is a few
    /// hundred bytes.
    std::unordered_map<ItemId, std::vector<ItemQuad>> m_itemModels;
};

} // namespace mc
