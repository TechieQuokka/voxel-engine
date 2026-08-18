#include "render/ItemRenderer.hpp"

#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "world/BlockTable.hpp"
#include "world/FallingBlocks.hpp"
#include "world/ItemEntities.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace mc {

ItemRenderer::ItemRenderer()
    : m_shader(rhi::Shader::fromFiles(assetPath("shaders/item.vert"),
                                      assetPath("shaders/item.frag"))) {
    m_shader.setUniform("u_blockTextures", static_cast<i32>(kTextureUnit));
    m_gpuItems.reserve(kInitialCapacity);
}

void ItemRenderer::draw(rhi::Device& device, const Camera& camera,
                        const BlockTextures& textures, const ItemEntities& items,
                        const FallingBlocks& falling, f32 spinSeconds,
                        rhi::FrameRing& ring) {
    MC_PROFILE_SCOPE_N("ItemRenderer::draw");

    m_drawn = 0;
    m_gpuItems.clear();

    const f32 yaw = spinSeconds * kSpinRate;

    for (const ItemEntity& item : items.items()) {
        if (!itemExists(item.item)) {
            continue;
        }

        // The bob is driven by the item's own age rather than by the shared clock,
        // so a pile dropped at different moments does not pulse in unison like one
        // object. The spin is shared, which reads as intentional; a shared bob reads
        // as a bug.
        const f32 bob = std::sin(item.age * 2.2f) * kBobHeight;

        m_gpuItems.push_back(GpuItem{
            vec4{item.position.x, item.position.y + kHalfExtent + bob, item.position.z,
                 kHalfExtent},
            // A block item draws its top layer, because that is the face people
            // recognise -- grass reads as grass rather than as a dirt cube with a
            // green lid. A loose item draws its icon, and `itemIcon` answers both.
            vec4{yaw, static_cast<f32>(itemIcon(item.item)), 0.0f, 0.0f},
        });
    }

    // Falling blocks go in the same buffer, which is the whole reason there is no
    // fourth render path. Full size, no spin and no bob: a block of sand coming down
    // should read as the terrain it is about to become, and any motion of its own
    // would say "entity" instead.
    for (const FallingBlock& block : falling.blocks()) {
        if (block.block == kAirBlock || block.block >= kBlocks.size()) {
            continue;
        }

        // `y` is the bottom face, and the shader wants the centre.
        m_gpuItems.push_back(GpuItem{
            vec4{static_cast<f32>(block.x) + 0.5f, block.y + kFallingHalfExtent,
                 static_cast<f32>(block.z) + 0.5f, kFallingHalfExtent},
            vec4{0.0f, static_cast<f32>(kBlocks[block.block].top), 0.0f, 0.0f},
        });
    }

    if (m_gpuItems.empty()) {
        return;
    }

    const std::optional<rhi::FrameRing::Slice> slice = ring.upload(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(m_gpuItems.data()),
                                   m_gpuItems.size() * sizeof(GpuItem)});
    if (!slice.has_value()) {
        return;
    }

    rhi::Buffer::barrierAfterClientWrites();

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());

    textures.bind(kTextureUnit);
    ring.bind(rhi::BufferTarget::Storage, kItemBufferBinding, *slice);
    m_vao.bind();

    m_drawn = m_gpuItems.size();
    device.drawTriangles(static_cast<u32>(m_gpuItems.size()) * kVerticesPerCube);
}

} // namespace mc
