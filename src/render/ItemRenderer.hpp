#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/BlockTextures.hpp"
#include "render/Camera.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"

#include <optional>
#include <vector>

namespace mc {

class ItemEntities;

/// Draws every dropped item in the world, in one call.
///
/// The third render path, and the cheapest of them. Each item is a spinning cube one
/// quarter of a block across; the SSBO carries a centre, a yaw and a texture layer,
/// and the cube's 36 vertices come out of `gl_VertexID` exactly as the chunk and
/// character paths do theirs. No vertex buffer, no per-item draw.
///
/// It borrows `BlockTextures` rather than owning one -- an item *is* a block here,
/// so it has to use the same layer the terrain does or a dropped stone would not
/// match the stone it came from.
class ItemRenderer {
public:
    ItemRenderer();

    /// `spinSeconds` is a clock, not a delta: every item shares one rotation so a
    /// pile turns together, which is what Minecraft does and what stops a heap
    /// looking like scattered debris.
    void draw(rhi::Device& device, const Camera& camera, const BlockTextures& textures,
              const ItemEntities& items, f32 spinSeconds);

    usize drawnLastFrame() const noexcept { return m_drawn; }

private:
    /// Matches `Item` in item.vert.
    struct GpuItem {
        vec4 centre;   ///< xyz position, w half-extent
        vec4 rotation; ///< x yaw, y texture layer
    };

    /// Grown geometrically; a mining session that dropped this many once will do it
    /// again, and reallocating mid-frame is a dropped frame rather than a slow one.
    static constexpr usize kInitialCapacity = 256;
    static constexpr u32 kVerticesPerCube = 36;
    static constexpr u32 kItemBufferBinding = 0;
    static constexpr u32 kTextureUnit = 0;

    /// A quarter block, which is Minecraft's dropped-item size.
    static constexpr f32 kHalfExtent = 0.125f;
    /// How far the bob rises and falls, and how fast.
    static constexpr f32 kBobHeight = 0.06f;
    static constexpr f32 kSpinRate = 1.1f;

    void ensureCapacity(usize count);

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;
    std::optional<rhi::Buffer> m_buffer;
    usize m_capacity = 0;

    std::vector<GpuItem> m_gpuItems;
    usize m_drawn = 0;
};

} // namespace mc
