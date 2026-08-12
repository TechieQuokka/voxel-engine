#version 460 core

in vec3 v_normal;
in vec3 v_worldPos;
in vec2 v_uv;
in float v_ao;
in float v_light;
flat in uint v_layer;

layout(binding = 0) uniform sampler2DArray u_blockTextures;

uniform vec3 u_cameraPosition;
uniform float u_aoStrength;
uniform float u_fadeDistance;
/// Linear, like everything else here. Matches the clear colour.
uniform vec3 u_fogColor;

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

    // Sky light multiplies in alongside AO, both already linear. They answer
    // different questions -- AO is how enclosed this corner is, light is how much
    // daylight reaches it -- and a surface needs to be dark when either says so.
    vec3 shaded = albedo.rgb * faceShading(v_normal) * ao * v_light;

    // Distance fog, as a mix toward the sky rather than a multiply toward black.
    //
    // The multiply was a stand-in and it stopped being defensible once shading moved
    // to linear space: a linear ramp toward zero reads as terrain turning black at
    // the render distance, which looks like a bug. Blending toward the sky colour is
    // both what fog physically is -- light scattered in, not light removed -- and
    // what makes the edge of the loaded region disappear instead of announcing
    // itself. Phase 7 replaces the falloff with something depth-based; the operation
    // is already the right one.
    //
    // Fog starts partway out rather than at the camera, or nearby terrain would be
    // visibly washed out.
    float distance = length(v_worldPos - u_cameraPosition);
    float fog = smoothstep(u_fadeDistance * 0.55, u_fadeDistance, distance);

    // Alpha comes from the texture rather than being 1. Every opaque block layer
    // stores 255, so this changes nothing for them; water stores ~0.75 and is drawn
    // in the translucent pass with blending on. See BlockTable's water layer.
    o_fragColor = vec4(mix(shaded, u_fogColor, fog), albedo.a);
}
