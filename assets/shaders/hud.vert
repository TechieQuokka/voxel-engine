#version 460 core

// Screen-space quads: hotbar slots, block icons, digits, crosshair.
//
// One SSBO entry per quad, six vertices from gl_VertexID, no vertex buffer and no
// projection matrix -- rects arrive already in normalised device coordinates,
// because the only transform a HUD needs is one the CPU can do once per quad.

struct HudQuad {
    vec4 rect;   // x0, y0, x1, y1 in NDC
    vec4 tint;   // multiplied into whatever the mode produces
    vec4 params; // x = mode, y = texture layer
};

layout(std430, binding = 0) readonly buffer Quads {
    HudQuad quads[];
};

out vec2 v_uv;
out vec4 v_tint;
flat out int v_mode;
flat out int v_layer;

const vec2 kCorner[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    HudQuad quad = quads[gl_VertexID / 6];
    vec2 corner = kCorner[gl_VertexID % 6];

    v_uv = vec2(corner.x, 1.0 - corner.y); // Textures are top-row-first.
    v_tint = quad.tint;
    v_mode = int(quad.params.x);
    v_layer = int(quad.params.y);

    gl_Position = vec4(mix(quad.rect.xy, quad.rect.zw, corner), 0.0, 1.0);
}
