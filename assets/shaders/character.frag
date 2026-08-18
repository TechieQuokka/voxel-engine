#version 460 core

in vec3 v_normal;
in vec3 v_color;
in vec2 v_uv;
flat in float v_layer;

out vec4 o_fragColor;

// The block texture array, which carries item icons too. Bound only because the
// character can now be holding something; the player model itself is flat colours
// and samples nothing.
uniform sampler2DArray u_blockTextures;

// Identical to chunk.frag's, and identical on purpose: the character has to sit in
// the same light as the terrain it stands on. Two different face-brightness tables
// would make it read as a sticker on the world rather than as part of it.
//
// The literals are perceptual, so they are written as sRGB and decoded here. Every
// argument is constant, so the driver folds this rather than paying per fragment.
float srgbToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float faceShading(vec3 normal) {
    if (normal.y > 0.5)  return srgbToLinear(1.00);
    if (normal.y < -0.5) return srgbToLinear(0.55);
    if (abs(normal.x) > 0.5) return srgbToLinear(0.75);
    return srgbToLinear(0.85);
}

void main() {
    vec3 albedo = v_color;

    if (v_layer >= 0.0) {
        // A held item: the two flat faces of an extruded sprite, or the six faces of
        // a held block. The array is sRGB so the sampler has already decoded.
        vec4 texel = texture(u_blockTextures, vec3(v_uv, v_layer));

        // **The discard is what gives a tool its shape.** The sprite is a 16x16 tile
        // with a transparent background and the model is one quad; without this a
        // held pickaxe is a square card with a pickaxe painted on it. Rim faces
        // carry a colour rather than a texture and never reach this branch.
        if (texel.a < 0.5) {
            discard;
        }
        albedo = texel.rgb;
    }

    // v_color arrives linear; the framebuffer is sRGB and encodes on write.
    o_fragColor = vec4(albedo * faceShading(v_normal), 1.0);
}
