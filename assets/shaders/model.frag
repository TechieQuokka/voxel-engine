#version 460 core

// The model pass's fragment rule. Identical to chunk.frag with one thing missing:
// there is no ambient occlusion, because a ModelBox has no room for per-corner data
// and the surfaces are small enough that a gradient across one would rarely show.
// See mesh/ModelBox.hpp for what the format gives up and why.

in vec3 v_normal;
in vec3 v_worldPos;
in vec2 v_uv;
in float v_light;
flat in uint v_layer;

layout(binding = 0) uniform sampler2DArray u_blockTextures;

uniform vec3 u_cameraPosition;
uniform float u_fadeDistance;
uniform vec3 u_fogColor;

out vec4 o_fragColor;

float srgbToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// **The same table as chunk.frag, and it has to stay the same.** A slab cut from a
// block sits against full blocks of the same material; if its top face were shaded by
// a different constant, the join would read as a different material rather than as the
// same one cut in half.
float faceShading(vec3 normal) {
    if (normal.y > 0.5)  return srgbToLinear(1.00);   // top
    if (normal.y < -0.5) return srgbToLinear(0.55);   // bottom
    if (abs(normal.x) > 0.5) return srgbToLinear(0.75);
    return srgbToLinear(0.85);                        // +/- Z
}

void main() {
    vec4 albedo = texture(u_blockTextures, vec3(v_uv, float(v_layer)));

    vec3 shaded = albedo.rgb * faceShading(v_normal) * v_light;

    float distance = length(v_worldPos - u_cameraPosition);
    float fog = smoothstep(u_fadeDistance * 0.55, u_fadeDistance, distance);

    o_fragColor = vec4(mix(shaded, u_fogColor, fog), albedo.a);
}
