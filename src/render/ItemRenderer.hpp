#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/BlockTextures.hpp"
#include "render/Camera.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/FrameRing.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"

#include <vector>

namespace mc {

class ItemEntities;
class FallingBlocks;

/// Draws every dropped item and every falling block in the world, in one call.
///
/// The third render path, and the cheapest of them. Each entity is a cube; the SSBO
/// carries a centre, a half-extent, a yaw and a texture layer, and the cube's 36
/// vertices come out of `gl_VertexID` exactly as the chunk and character paths do
/// theirs. No vertex buffer, no per-entity draw.
///
/// **Falling blocks cost nothing here beyond the loop that appends them.** The
/// half-extent was already a per-entity field rather than a constant, so a falling
/// sand block is the same struct with 0.5 in it and no spin -- same shader, same
/// buffer, same draw. That was luck rather than foresight, but it is why Phase 12
/// needed no fourth render path.
///
/// It borrows `BlockTextures` rather than owning one -- an item *is* a block here,
/// so it has to use the same layer the terrain does or a dropped stone would not
/// match the stone it came from.
class ItemRenderer {
public:
    ItemRenderer();

    /// `spinSeconds` is a clock, not a delta: every item shares one rotation so a
    /// pile turns together, which is what Minecraft does and what stops a heap
    /// looking like scattered debris. Falling blocks ignore it -- a tumbling block
    /// of sand is not what vanilla does and reads as debris rather than as terrain
    /// coming down.
    void draw(rhi::Device& device, const Camera& camera, const BlockTextures& textures,
              const ItemEntities& items, const FallingBlocks& falling, f32 spinSeconds,
              rhi::FrameRing& ring);

    usize drawnLastFrame() const noexcept { return m_drawn; }

private:
    /// Matches `Item` in item.vert.
    struct GpuItem {
        vec4 centre;   ///< xyz position, w half-extent
        vec4 rotation; ///< x yaw, y texture layer
    };

    /// What the CPU-side list reserves. The GPU side is the shared frame ring, so
    /// this is a vector's capacity and nothing more.
    static constexpr usize kInitialCapacity = 256;
    static constexpr u32 kVerticesPerCube = 36;
    static constexpr u32 kItemBufferBinding = 0;
    static constexpr u32 kTextureUnit = 0;

    /// A quarter block, which is Minecraft's dropped-item size.
    static constexpr f32 kHalfExtent = 0.125f;
    /// A falling block is a whole block, because it is one.
    static constexpr f32 kFallingHalfExtent = 0.5f;
    /// How far the bob rises and falls, and how fast.
    static constexpr f32 kBobHeight = 0.06f;
    static constexpr f32 kSpinRate = 1.1f;

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;

    std::vector<GpuItem> m_gpuItems;
    usize m_drawn = 0;
};

} // namespace mc
