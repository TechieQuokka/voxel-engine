#version 460 core

// Dropped items: a small block, spinning and bobbing above the ground.
//
// No vertex buffer. One SSBO entry per item carries a centre, a rotation and which
// texture layer to use; the 36 vertices of the cube come from gl_VertexID, and
// gl_VertexID / 36 picks the item. That means **every dropped item in the world is
// one draw call**, which is the same trick the chunk renderer plays at a different
// scale.

struct Item {
    vec4 centre;    // xyz world position, w = half size
    vec4 rotation;  // x = yaw in radians, y = texture layer, zw unused
};

layout(std430, binding = 0) readonly buffer Items {
    Item items[];
};

uniform mat4 u_viewProjection;

out vec3 v_uvLayer;
out float v_shade;

// Face order matches world/Coords.hpp: -x, +x, -y, +y, -z, +z.
const vec3 kOrigin[6] = vec3[6](
    vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 1.0),
    vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0)
);
const vec3 kAxisU[6] = vec3[6](
    vec3(0.0, 0.0, -1.0), vec3(0.0, 0.0, 1.0),
    vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),
    vec3(-1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0)
);
const vec3 kAxisV[6] = vec3[6](
    vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0),
    vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0)
);
const vec2 kCorner[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

// The same per-face brightness chunk.frag uses, so a dropped block is lit like the
// ground it is lying on rather than reading as a sticker.
const float kFaceShade[6] = float[6](0.72, 0.72, 0.5, 1.0, 0.86, 0.86);

void main() {
    int itemIndex = gl_VertexID / 36;
    int vertex = gl_VertexID % 36;
    int face = vertex / 6;

    Item item = items[itemIndex];
    vec2 corner = kCorner[vertex % 6];

    // Unit cube about its own centre, then scaled and spun about Y.
    vec3 unit = kOrigin[face] + kAxisU[face] * corner.x + kAxisV[face] * corner.y;
    vec3 local = (unit - vec3(0.5)) * (2.0 * item.centre.w);

    float yaw = item.rotation.x;
    float c = cos(yaw);
    float s = sin(yaw);
    vec3 spun = vec3(local.x * c - local.z * s, local.y, local.x * s + local.z * c);

    v_uvLayer = vec3(corner, item.rotation.y);
    v_shade = kFaceShade[face];

    gl_Position = u_viewProjection * vec4(item.centre.xyz + spun, 1.0);
}
