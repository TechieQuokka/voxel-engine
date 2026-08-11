#version 460 core

// The breaking overlay: a unit cube over the block being mined, textured with the
// destroy stage.
//
// No vertex buffer, like everything else here. Six faces of six vertices are
// expanded from gl_VertexID against the tables below, which is the same trick
// character.vert uses -- an origin corner plus two edge vectors per face.

uniform mat4 u_viewProjection;
uniform vec3 u_blockOrigin;
uniform float u_inflate;

out vec2 v_uv;

// Face order matches world/Coords.hpp: -x, +x, -y, +y, -z, +z. Wound so the
// outward face is counter-clockwise, matching the engine's front-face convention.
const vec3 kOrigin[6] = vec3[6](
    vec3(0.0, 0.0, 1.0),  // -x
    vec3(1.0, 0.0, 0.0),  // +x
    vec3(0.0, 0.0, 0.0),  // -y
    vec3(0.0, 1.0, 1.0),  // +y
    vec3(1.0, 0.0, 0.0),  // -z
    vec3(0.0, 0.0, 1.0)   // +z
);

const vec3 kAxisU[6] = vec3[6](
    vec3(0.0, 0.0, -1.0),
    vec3(0.0, 0.0, 1.0),
    vec3(1.0, 0.0, 0.0),
    vec3(1.0, 0.0, 0.0),
    vec3(-1.0, 0.0, 0.0),
    vec3(1.0, 0.0, 0.0)
);

const vec3 kAxisV[6] = vec3[6](
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(0.0, 0.0, -1.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 1.0, 0.0)
);

// Two triangles over the unit square.
const vec2 kCorner[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    int face = gl_VertexID / 6;
    vec2 corner = kCorner[gl_VertexID % 6];

    vec3 unit = kOrigin[face] + kAxisU[face] * corner.x + kAxisV[face] * corner.y;

    // Pushed out from the cube's centre so the overlay wins the depth test against
    // the block's own faces, which lie in exactly the same planes.
    vec3 offset = (unit - vec3(0.5)) * (2.0 * u_inflate);

    v_uv = corner;
    gl_Position = u_viewProjection * vec4(u_blockOrigin + unit + offset, 1.0);
}
