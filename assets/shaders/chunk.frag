#version 460 core

in vec3 v_normal;
in vec3 v_worldPos;
in vec2 v_uv;
in float v_ao;
flat in uint v_layer;

layout(binding = 0) uniform sampler2DArray u_blockTextures;

uniform vec3 u_cameraPosition;
uniform float u_aoStrength;

out vec4 o_fragColor;

// Fixed per-face brightness rather than a real light. This is what makes voxel
// geometry readable: identical shading on adjacent faces would otherwise merge
// into a flat silhouette.
float faceShading(vec3 normal) {
    if (normal.y > 0.5)  return 1.00;   // top
    if (normal.y < -0.5) return 0.55;   // bottom
    if (abs(normal.x) > 0.5) return 0.75;
    return 0.85;                        // +/- Z
}

void main() {
    vec4 albedo = texture(u_blockTextures, vec3(v_uv, float(v_layer)));

    // v_ao is 1 for fully open corners and 0 for fully occluded ones.
    float ao = mix(1.0, v_ao, u_aoStrength);

    vec3 shaded = albedo.rgb * faceShading(v_normal) * ao;

    // Cheap distance darkening so depth reads correctly without a fog system.
    float distance = length(v_worldPos - u_cameraPosition);
    float fade = clamp(1.0 - distance / 400.0, 0.35, 1.0);

    o_fragColor = vec4(shaded * fade, 1.0);
}
