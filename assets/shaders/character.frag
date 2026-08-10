#version 460 core

in vec3 v_normal;
in vec3 v_color;

out vec4 o_fragColor;

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
    // v_color arrives linear; the framebuffer is sRGB and encodes on write.
    o_fragColor = vec4(v_color * faceShading(v_normal), 1.0);
}
