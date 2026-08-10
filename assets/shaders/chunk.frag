#version 460 core

in vec3 v_normal;
in vec3 v_worldPos;
in vec2 v_uv;
in float v_ao;
flat in uint v_layer;

layout(binding = 0) uniform sampler2DArray u_blockTextures;

uniform vec3 u_cameraPosition;
uniform float u_aoStrength;
uniform float u_fadeDistance;

out vec4 o_fragColor;

// Everything here happens in linear space: the block texture is SRGB8_ALPHA8 so
// texture() already returns linear values, and GL_FRAMEBUFFER_SRGB encodes the
// result on write. Multiplying sRGB-encoded values, which is what this shader
// used to do, is the wrong operation for quantities that behave like light.
//
// The shading factors below are a different matter -- they were chosen by eye,
// so they are perceptual numbers. Writing them as sRGB and decoding here keeps
// them readable as what they mean. Every argument is a literal, so the driver
// folds the conversion instead of paying for it per fragment.
float srgbToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// Fixed per-face brightness rather than a real light. This is what makes voxel
// geometry readable: identical shading on adjacent faces would otherwise merge
// into a flat silhouette.
float faceShading(vec3 normal) {
    if (normal.y > 0.5)  return srgbToLinear(1.00);   // top
    if (normal.y < -0.5) return srgbToLinear(0.55);   // bottom
    if (abs(normal.x) > 0.5) return srgbToLinear(0.75);
    return srgbToLinear(0.85);                        // +/- Z
}

void main() {
    vec4 albedo = texture(u_blockTextures, vec3(v_uv, float(v_layer)));

    // v_ao arrives already linear -- the vertex shader decodes the packed level
    // through the same curve, so interpolation across a merged quad happens in
    // light space. 1 is a fully open corner, 0 a fully occluded one.
    float ao = mix(1.0, v_ao, u_aoStrength);

    vec3 shaded = albedo.rgb * faceShading(v_normal) * ao;

    // Cheap distance darkening so depth reads correctly without a fog system.
    // The floor is the linear equivalent of 0.35 sRGB, but the ramp between it
    // and 1.0 is now linear in light rather than in perception, so distance
    // darkening sets in somewhat harder than it did. A real fog pass -- a mix
    // toward a sky colour, not a multiply -- replaces this when the far field
    // arrives in Phase 7.
    float distance = length(v_worldPos - u_cameraPosition);
    float fade = clamp(1.0 - distance / u_fadeDistance, srgbToLinear(0.35), 1.0);

    o_fragColor = vec4(shaded * fade, 1.0);
}
