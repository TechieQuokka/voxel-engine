#version 460 core

// Placeholder shading until the texture array lands in Phase 2. Colours come
// from BlockRegistry so this shader never becomes a second source of truth for
// block appearance.

in vec3 v_normal;
in vec3 v_worldPos;
flat in uint v_material;

uniform vec3 u_cameraPosition;
uniform vec4 u_blockColors[16];

out vec4 o_fragColor;

// Fixed per-face brightness rather than a real light. This is what makes voxel
// geometry readable without textures: identical colours on adjacent faces would
// otherwise merge into a flat silhouette.
float faceShading(vec3 normal) {
    if (normal.y > 0.5)  return 1.00;   // top
    if (normal.y < -0.5) return 0.55;   // bottom
    if (abs(normal.x) > 0.5) return 0.75;
    return 0.85;                        // +/- Z
}

void main() {
    vec4 baseColor = u_blockColors[v_material & 0xFu];
    vec3 shaded = baseColor.rgb * faceShading(v_normal);

    // Cheap distance darkening so depth reads correctly without a fog system.
    float distance = length(v_worldPos - u_cameraPosition);
    float fade = clamp(1.0 - distance / 400.0, 0.35, 1.0);

    o_fragColor = vec4(shaded * fade, 1.0);
}
