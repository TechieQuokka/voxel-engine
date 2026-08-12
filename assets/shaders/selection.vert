#version 460 core

// The wireframe box around the block the player is pointing at.
//
// No vertex buffer, like everything else here. **Each of the twelve edges is a quad
// rather than a GL line**, which is six vertices apiece and seventy-two in total,
// still derived entirely from gl_VertexID against the table below.
//
// It used to be `glLineWidth`-width lines. That is one pixel and stays one pixel:
// rhi/Device.hpp records that a core profile is not required to support anything
// wider and that a driver may silently ignore the request, which is why this was
// never simply turned up. A one-pixel black outline is hard to pick out of a
// textured world at a glance -- reported from play as not being able to tell which
// block was about to be mined -- so the width is done here instead, where it is
// exact and driver-independent.
//
// The thickness is applied in **screen space**, so the outline reads the same at
// arm's length and at the far end of the five-block reach. Expanding in world space
// would make a distant box thinner precisely when it is already hardest to see.
//
// The box is a unit cube at u_blockOrigin, inflated slightly so its faces do not
// z-fight with the block's own -- which they would do exactly, being the same planes.

uniform mat4 u_viewProjection;
uniform vec3 u_blockOrigin;
uniform float u_inflate;
/// Half-thickness, in NDC height units. NDC spans 2.0 top to bottom, so the drawn
/// width in pixels is roughly u_thickness * framebufferHeight.
uniform float u_thickness;
/// Width over height, so the offset stays circular rather than stretching with the
/// window. Fullscreen made this visible: an outline tuned at 1280x720 went lopsided.
uniform float u_aspect;

// The twelve edges of a cube, as pairs of corner indices. A corner index is read
// as three bits: bit 0 is x, bit 1 is y, bit 2 is z.
const uint kEdges[24] = uint[24](
    // Four along x.
    0u, 1u,  2u, 3u,  4u, 5u,  6u, 7u,
    // Four along y.
    0u, 2u,  1u, 3u,  4u, 6u,  5u, 7u,
    // Four along z.
    0u, 4u,  1u, 5u,  2u, 6u,  3u, 7u
);

vec4 cornerClip(uint corner) {
    vec3 unit = vec3(float(corner & 1u),
                     float((corner >> 1) & 1u),
                     float((corner >> 2) & 1u));

    // Push each corner outward from the cube's centre rather than scaling about the
    // origin, so the box stays centred on the block whatever the inflation is.
    vec3 offset = (unit - vec3(0.5)) * (2.0 * u_inflate);

    return u_viewProjection * vec4(u_blockOrigin + unit + offset, 1.0);
}

void main() {
    uint edge = uint(gl_VertexID) / 6u;
    uint vert = uint(gl_VertexID) % 6u;

    vec4 clipA = cornerClip(kEdges[edge * 2u]);
    vec4 clipB = cornerClip(kEdges[edge * 2u + 1u]);

    // Two triangles: (A-, B-, A+) and (A+, B-, B+).
    bool atB = (vert == 1u || vert == 4u || vert == 5u);
    float side = (vert == 0u || vert == 1u || vert == 4u) ? -1.0 : 1.0;

    vec4 clip = atB ? clipB : clipA;

    // Screen-space direction of the edge, aspect-corrected so "perpendicular" means
    // perpendicular on the display rather than in NDC.
    //
    // The w guard matters: this box sits within five blocks of the camera, but a
    // corner can still land behind the near plane when the player stands inside the
    // block being mined, and dividing by a w at or below zero mirrors the segment.
    vec2 screenA = clipA.xy / max(clipA.w, 1e-4);
    vec2 screenB = clipB.xy / max(clipB.w, 1e-4);

    vec2 delta = (screenB - screenA) * vec2(u_aspect, 1.0);
    // A degenerate edge -- one seen exactly end-on -- has no direction to be
    // perpendicular to. Any fixed normal will do, because the quad is a point.
    vec2 direction = length(delta) > 1e-6 ? normalize(delta) : vec2(1.0, 0.0);
    vec2 normal = vec2(-direction.y, direction.x) / vec2(u_aspect, 1.0);

    // Scaled by w because the offset is applied before the perspective divide.
    clip.xy += normal * (side * u_thickness * clip.w);

    gl_Position = clip;
}
