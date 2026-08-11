#version 460 core

// The wireframe box around the block the player is pointing at.
//
// No vertex buffer, like everything else here: twelve edges are twenty-four
// vertices, and each one is derived from gl_VertexID against the tables below.
// The box is a unit cube at u_blockOrigin, inflated slightly so its edges do not
// z-fight with the block's own faces -- which they would do exactly, being the
// same planes.

uniform mat4 u_viewProjection;
uniform vec3 u_blockOrigin;
uniform float u_inflate;

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

void main() {
    uint corner = kEdges[gl_VertexID];

    vec3 unit = vec3(float(corner & 1u),
                     float((corner >> 1) & 1u),
                     float((corner >> 2) & 1u));

    // Push each corner outward from the cube's centre rather than scaling about the
    // origin, so the box stays centred on the block whatever the inflation is.
    vec3 offset = (unit - vec3(0.5)) * (2.0 * u_inflate);

    gl_Position = u_viewProjection * vec4(u_blockOrigin + unit + offset, 1.0);
}
