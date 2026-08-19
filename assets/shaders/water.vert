#version 460 core

// The translucent pass. Same quad arena, same section-origin buffer and same
// vertex pulling as chunk.vert -- what differs is that bits 33..40 are not ambient
// occlusion here.
//
// Packing must match mesh/Quad.hpp:
//   bits  0..5   x        bits 18..23  width - 1    bits 33..40  corner drops
//   bits  6..11  y        bits 24..29  height - 1   bits 41..56  light
//   bits 12..17  z        bits 30..32  face         bits 57..63  material
//
// **A fluid quad spends those eight bits on four corner drops instead**, two bits
// each, because a water surface has a height and AO on water means nothing. See the
// note in Quad.hpp for why the field was free and what the two bits cost.

layout(std430, binding = 0) readonly buffer QuadBuffer {
    uvec2 b_quads[];
};

layout(std430, binding = 1) readonly buffer SectionBuffer {
    vec4 b_sectionOrigins[];
};

uniform mat4 u_viewProjection;

// Where this draw's sections start in b_sectionOrigins. The water pass runs second
// over one shared list, so this is the count of opaque sections rather than zero.
uniform int u_drawIdBase;

out vec3 v_normal;
out vec3 v_worldPos;
out vec2 v_uv;
out float v_light;
flat out uint v_layer;
// Which way this quad's surface runs downhill, in its own UV space, or zero where
// the question does not apply. The fragment shader scrolls the texture along it.
flat out vec2 v_flow;

const vec3 kNormals[6] = vec3[6](
    vec3(-1.0,  0.0,  0.0),   // NegX
    vec3( 1.0,  0.0,  0.0),   // PosX
    vec3( 0.0, -1.0,  0.0),   // NegY
    vec3( 0.0,  1.0,  0.0),   // PosY
    vec3( 0.0,  0.0, -1.0),   // NegZ
    vec3( 0.0,  0.0,  1.0)    // PosZ
);

const vec3 kTangentU[6] = vec3[6](
    vec3(0.0, 0.0, 1.0),   // NegX
    vec3(0.0, 1.0, 0.0),   // PosX
    vec3(1.0, 0.0, 0.0),   // NegY
    vec3(0.0, 0.0, 1.0),   // PosY
    vec3(0.0, 1.0, 0.0),   // NegZ
    vec3(1.0, 0.0, 0.0)    // PosZ
);

const vec3 kTangentV[6] = vec3[6](
    vec3(0.0, 1.0, 0.0),   // NegX
    vec3(0.0, 0.0, 1.0),   // PosX
    vec3(0.0, 0.0, 1.0),   // NegY
    vec3(1.0, 0.0, 0.0),   // PosY
    vec3(1.0, 0.0, 0.0),   // NegZ
    vec3(0.0, 1.0, 0.0)    // PosZ
);

const vec2 kCorners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

const uint kCornerIds[6] = uint[6](0u, 1u, 2u, 0u, 2u, 3u);

// How far below the top of its block each drop code puts the surface, in blocks.
//
// **This table is the whole of what two bits buys, and it is not evenly spaced.**
// 0 is a full block, which is every submerged block in an ocean and also what any
// quad built without thinking about fluids gets for free. 1/9 is vanilla's source
// surface -- the reason a shoreline has a visible lip. The last two cover the five
// flowing levels between them, ending at 8/9, which is a stream one ninth deep.
const float kDrop[4] = float[4](0.0, 1.0 / 9.0, 4.0 / 9.0, 8.0 / 9.0);

const float kLightLinear[16] = float[16](
    0.035, 0.044, 0.055, 0.069, 0.086, 0.107, 0.134, 0.168,
    0.210, 0.262, 0.328, 0.410, 0.512, 0.640, 0.800, 1.000
);

void main() {
    uint quadIndex = uint(gl_VertexID) / 6u;
    uint cornerIndex = uint(gl_VertexID) % 6u;

    vec3 sectionOrigin = b_sectionOrigins[u_drawIdBase + gl_DrawID].xyz;

    uvec2 quad = b_quads[quadIndex];
    uint lo = quad.x;
    uint hi = quad.y;

    vec3 origin = vec3(float(lo & 0x3Fu),
                       float((lo >> 6) & 0x3Fu),
                       float((lo >> 12) & 0x3Fu));

    float width  = float((lo >> 18) & 0x3Fu) + 1.0;
    float height = float((lo >> 24) & 0x3Fu) + 1.0;

    uint face = ((lo >> 30) & 0x3u) | ((hi & 0x1u) << 2);

    uint dropBits = (hi >> 1) & 0xFFu;
    uint lightBits = (hi >> 9) & 0xFFFFu;
    v_layer = (hi >> 25) & 0x7Fu;

    vec2 corner = kCorners[cornerIndex];
    vec3 localPos = origin
                  + kTangentU[face] * (corner.x * width)
                  + kTangentV[face] * (corner.y * height);

    uint cornerId = kCornerIds[cornerIndex];

    // **Lower this vertex, and only this vertex.** The mesher already answered the
    // question per corner and wrote zero for any vertex that is not on the top of
    // its block, so a bottom face does not move, a side face's lower edge does not
    // move, and its upper edge lands exactly on the surface the top face draws. No
    // per-face branch is needed here and that is the point of encoding it that way.
    localPos.y -= kDrop[(dropBits >> (2u * cornerId)) & 0x3u];

    v_uv = vec2(corner.x * width, corner.y * height);

    uint light = (lightBits >> (4u * cornerId)) & 0xFu;
    v_light = kLightLinear[light];

    // Which way the surface falls, from the four drops, in the quad's own UV space.
    // Only a top face has one: on a side face the lower corners are pinned at zero,
    // so the same arithmetic would report a slope that is really just the wall.
    v_flow = vec2(0.0);
    if (face == 3u) {
        float d0 = kDrop[dropBits & 0x3u];
        float d1 = kDrop[(dropBits >> 2u) & 0x3u];
        float d2 = kDrop[(dropBits >> 4u) & 0x3u];
        float d3 = kDrop[(dropBits >> 6u) & 0x3u];
        // Corners are (0,0) (1,0) (1,1) (0,1), and a bigger drop is further down.
        v_flow = vec2((d1 + d2) - (d0 + d3), (d2 + d3) - (d0 + d1)) * 0.5;
    }

    v_worldPos = sectionOrigin + localPos;
    v_normal = kNormals[face];

    gl_Position = u_viewProjection * vec4(v_worldPos, 1.0);
}
