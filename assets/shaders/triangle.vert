#version 460 core

// Attribute-less: the triangle is generated from gl_VertexID with no vertex
// buffer bound. This is the same technique the chunk renderer uses to expand
// packed quads (DESIGN.md 3.7), exercised here in its simplest form.

out vec3 v_color;

const vec2 kPositions[3] = vec2[3](
    vec2( 0.0,  0.6),
    vec2(-0.6, -0.4),
    vec2( 0.6, -0.4)
);

const vec3 kColors[3] = vec3[3](
    vec3(1.0, 0.35, 0.35),
    vec3(0.35, 1.0, 0.45),
    vec3(0.40, 0.55, 1.0)
);

void main() {
    v_color = kColors[gl_VertexID];
    gl_Position = vec4(kPositions[gl_VertexID], 0.0, 1.0);
}
