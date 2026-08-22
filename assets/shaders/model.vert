#version 460 core

// The model pass: non-cube geometry -- slabs, and later stairs, doors and fences.
//
// Programmatic vertex pulling, like chunk.vert, but the unit is a **box** rather than
// a face. One 64-bit word expands to six faces of two triangles, so gl_VertexID
// selects both which box and which of its 36 vertices to emit.
//
// Packing must match mesh/ModelBox.hpp:
//   bits  0.. 4  block x     bits 27..30  size x - 1   bits 40..46  material
//   bits  5.. 9  block y     bits 32..35  size y - 1   bits 47..50  sky light
//   bits 10..14  block z     bits 36..39  size z - 1   bits 51..54  block light
//   bits 15..18  min x
//   bits 19..22  min y
//   bits 23..26  min z

layout(std430, binding = 0) readonly buffer QuadBuffer {
    uvec2 b_quads[];
};

layout(std430, binding = 1) readonly buffer SectionBuffer {
    vec4 b_sectionOrigins[];
};

uniform mat4 u_viewProjection;
uniform int u_drawIdBase;

out vec3 v_normal;
out vec3 v_worldPos;
out vec2 v_uv;
out float v_light;
flat out uint v_layer;

// Same order as the Face enum in world/Coords.hpp, and the same winding rule as
// chunk.vert: cross(U, V) equals the normal, so every face reads counter-clockwise
// from outside and back-face culling works unchanged.
const vec3 kNormals[6] = vec3[6](
    vec3(-1.0,  0.0,  0.0),   // NegX
    vec3( 1.0,  0.0,  0.0),   // PosX
    vec3( 0.0, -1.0,  0.0),   // NegY
    vec3( 0.0,  1.0,  0.0),   // PosY
    vec3( 0.0,  0.0, -1.0),   // NegZ
    vec3( 0.0,  0.0,  1.0)    // PosZ
);

const vec3 kTangentU[6] = vec3[6](
    vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0),
    vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0)
);

const vec3 kTangentV[6] = vec3[6](
    vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0),
    vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0)
);

// Where each face's plane sits along its own normal: 0 for the three faces on the
// box's low side, 1 for the three on the high side.
const float kFaceOffset[6] = float[6](0.0, 1.0, 0.0, 1.0, 0.0, 1.0);

const vec2 kCorners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

// Minecraft's falloff, 0.8^(15 - level). Identical to chunk.vert's table; a slab lit
// beside a full block must come out the same shade or the join between them reads as
// a seam.
const float kLightLinear[16] = float[16](
    0.035, 0.044, 0.055, 0.069, 0.086, 0.107, 0.134, 0.168,
    0.210, 0.262, 0.328, 0.410, 0.512, 0.640, 0.800, 1.000
);

void main() {
    uint boxIndex = uint(gl_VertexID) / 36u;
    uint vertexInBox = uint(gl_VertexID) % 36u;
    uint face = vertexInBox / 6u;
    uint cornerIndex = vertexInBox % 6u;

    vec3 sectionOrigin = b_sectionOrigins[u_drawIdBase + gl_DrawID].xyz;

    uvec2 box = b_quads[boxIndex];
    uint lo = box.x;
    uint hi = box.y;

    vec3 cell = vec3(float(lo & 0x1Fu),
                     float((lo >> 5) & 0x1Fu),
                     float((lo >> 10) & 0x1Fu));

    // Sixteenths, converted once here. The collision code in world/BlockShape.hpp
    // works from the same integers, so what is drawn and what is walked into cannot
    // disagree by rounding.
    const float kUnit = 1.0 / 16.0;
    vec3 boxMin = vec3(float((lo >> 15) & 0xFu),
                       float((lo >> 19) & 0xFu),
                       float((lo >> 23) & 0xFu)) * kUnit;

    vec3 boxSize = vec3(float((lo >> 27) & 0xFu) + 1.0,
                        float((hi >> 0)  & 0xFu) + 1.0,
                        float((hi >> 4)  & 0xFu) + 1.0) * kUnit;

    v_layer = (hi >> 8) & 0x7Fu;
    uint sky = (hi >> 15) & 0xFu;
    uint blockLight = (hi >> 19) & 0xFu;

    // **Combined with max, exactly as the greedy mesher does it for a Quad.** The two
    // are stored apart here because there was room; until there is a day/night cycle
    // to apply a scalar to the sky term, the answer is the same one the rest of the
    // world gets. See HANDOFF 1.1.
    v_light = kLightLinear[max(sky, blockLight)];

    vec2 corner = kCorners[cornerIndex];
    vec3 normal = kNormals[face];
    vec3 u = kTangentU[face];
    vec3 v = kTangentV[face];

    // The face's own extent is the box size projected onto its two tangents; its
    // position along the normal is the low or the high side of the box.
    float extentU = dot(abs(u), boxSize);
    float extentV = dot(abs(v), boxSize);
    float alongNormal = dot(abs(normal), boxSize) * kFaceOffset[face];

    vec3 localPos = cell + boxMin
                  + abs(normal) * alongNormal
                  + u * (corner.x * extentU)
                  + v * (corner.y * extentV);

    // UVs in blocks, so the texture keeps the scale it has on a full cube: a slab's
    // side shows the bottom half of the tile rather than the whole tile squashed.
    // GL_REPEAT on the array makes the offset free.
    vec2 uvOrigin = vec2(dot(abs(u), cell + boxMin), dot(abs(v), cell + boxMin));
    v_uv = uvOrigin + vec2(corner.x * extentU, corner.y * extentV);

    v_worldPos = sectionOrigin + localPos;
    v_normal = normal;

    gl_Position = u_viewProjection * vec4(v_worldPos, 1.0);
}
