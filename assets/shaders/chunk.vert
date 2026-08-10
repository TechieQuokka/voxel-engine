#version 460 core

// Programmatic vertex pulling: there is no vertex buffer and no index buffer.
// Each quad is 8 bytes in an SSBO, and gl_VertexID selects both which quad and
// which of its six vertices to emit.
//
// Packing must match mesh/Quad.hpp:
//   bits  0..5   x        bits 18..23  width - 1    bits 33..40  ao
//   bits  6..11  y        bits 24..29  height - 1   bits 41..56  material
//   bits 12..17  z        bits 30..32  face

layout(std430, binding = 0) readonly buffer QuadBuffer {
    uvec2 b_quads[];
};

// Per-section data, indexed by gl_DrawID -- the draw's position in the
// glMultiDrawArrays list. This is what removes per-draw uniforms entirely: the
// whole visible set is one GL call, and each section still finds its own origin.
// gl_DrawID is core in GLSL 4.60, so no extension directive is needed.
//
// xyz is the section's world-space corner in blocks. w is padding, because std430
// aligns a vec3 to 16 bytes anyway and saying so is clearer than relying on it.
layout(std430, binding = 1) readonly buffer SectionBuffer {
    vec4 b_sectionOrigins[];
};

uniform mat4 u_viewProjection;

out vec3 v_normal;
out vec3 v_worldPos;
out vec2 v_uv;
out float v_ao;
flat out uint v_layer;

// Tangent basis per face, chosen so that cross(U, V) equals the face normal.
// That makes every quad wind counter-clockwise when seen from outside, which is
// what lets back-face culling work. Order matches the Face enum in
// world/Coords.hpp and the kPlans table in BinaryGreedyMesher.cpp.
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

// Two triangles over the quad corners (0,0) (1,0) (1,1) (0,1).
const vec2 kCorners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

// Which packed AO corner each of the six vertices reads. Must match the corner
// order in computeAo() in BinaryGreedyMesher.cpp.
const uint kCornerIds[6] = uint[6](0u, 1u, 2u, 0u, 2u, 3u);

// The four AO levels as linear light multipliers. The packed 2-bit value is a
// perceptual level (0 fully occluded, 3 fully open), so these are 0, 1/3, 2/3
// and 1 put through the sRGB decode -- the same curve chunk.frag uses.
//
// Decoded here rather than in the fragment shader for two reasons: interpolation
// across a merged quad has to happen in linear space to be meaningful, and a
// per-vertex table lookup is free where a per-fragment pow() would not be. GLSL
// forbids function calls in a const initializer, so the values are written out.
const float kAoLinear[4] = float[4](0.0, 0.090842, 0.401978, 1.0);

void main() {
    // gl_VertexID is absolute in OpenGL -- it already includes the draw's `first`
    // -- so this indexes the shared quad arena directly, with no base offset.
    uint quadIndex = uint(gl_VertexID) / 6u;
    uint cornerIndex = uint(gl_VertexID) % 6u;

    vec3 sectionOrigin = b_sectionOrigins[gl_DrawID].xyz;

    // Not named `packed`: that is a reserved keyword in GLSL.
    uvec2 quad = b_quads[quadIndex];
    uint lo = quad.x;
    uint hi = quad.y;

    vec3 origin = vec3(float(lo & 0x3Fu),
                       float((lo >> 6) & 0x3Fu),
                       float((lo >> 12) & 0x3Fu));

    float width  = float((lo >> 18) & 0x3Fu) + 1.0;
    float height = float((lo >> 24) & 0x3Fu) + 1.0;

    // The face field straddles the 32-bit boundary: bits 30..31 are the top of
    // the low word and bit 32 is the bottom of the high word.
    uint face = ((lo >> 30) & 0x3u) | ((hi & 0x1u) << 2);

    uint aoBits = (hi >> 1) & 0xFFu;
    v_layer = (hi >> 9) & 0xFFFFu;

    vec2 corner = kCorners[cornerIndex];
    vec3 localPos = origin
                  + kTangentU[face] * (corner.x * width)
                  + kTangentV[face] * (corner.y * height);

    // UVs run 0..width and 0..height so a merged quad tiles its texture once
    // per block. This is only possible because block textures live in an array
    // with GL_REPEAT rather than in an atlas.
    v_uv = vec2(corner.x * width, corner.y * height);

    uint ao = (aoBits >> (2u * kCornerIds[cornerIndex])) & 0x3u;
    v_ao = kAoLinear[ao];

    v_worldPos = sectionOrigin + localPos;
    v_normal = kNormals[face];

    gl_Position = u_viewProjection * vec4(v_worldPos, 1.0);
}
