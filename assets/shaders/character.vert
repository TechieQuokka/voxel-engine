#version 460 core

// The same trick chunk.vert uses, in floating point instead of a voxel grid.
//
// There is no vertex buffer here either: each quad is four vec4s in an SSBO and
// gl_VertexID picks both the quad and which of its six vertices to emit. A chunk
// Quad cannot be reused for this because its coordinates are 6-bit integers on the
// block lattice, and a player is 0.5 blocks wide -- so the character carries an
// explicit origin and two edge vectors instead.
struct CharQuad {
    vec4 origin;  // xyz: world-space corner
    vec4 uAxis;   // xyz: first edge, full length
    vec4 vAxis;   // xyz: second edge, full length
    vec4 color;   // rgb: linear, already decoded on the CPU
};

layout(std430, binding = 0) readonly buffer CharQuadBuffer {
    CharQuad b_quads[];
};

uniform mat4 u_viewProjection;

out vec3 v_normal;
out vec3 v_color;

// Two triangles over the corners (0,0) (1,0) (1,1) (0,1), matching chunk.vert.
const vec2 kCorners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    uint quadIndex = uint(gl_VertexID) / 6u;
    uint cornerIndex = uint(gl_VertexID) % 6u;

    CharQuad quad = b_quads[quadIndex];
    vec2 corner = kCorners[cornerIndex];

    vec3 worldPos = quad.origin.xyz
                  + quad.uAxis.xyz * corner.x
                  + quad.vAxis.xyz * corner.y;

    // Built on the CPU so that cross(U, V) points out of the box, the same
    // winding rule the chunk mesher follows -- which is what lets one back-face
    // culling state serve both.
    v_normal = normalize(cross(quad.uAxis.xyz, quad.vAxis.xyz));
    v_color = quad.color.rgb;

    gl_Position = u_viewProjection * vec4(worldPos, 1.0);
}
