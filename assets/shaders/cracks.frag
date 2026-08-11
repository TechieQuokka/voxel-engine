#version 460 core

// Samples one destroy stage and blends it over whatever is already there.
//
// The texture is deliberately Rgba8 rather than Srgb8A8: it is a stencil, not a
// colour. Its RGB is a flat dark value and all the information is in the alpha, so
// an sRGB decode would do nothing useful and would darken the cracks unevenly.

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2DArray u_cracks;
uniform int u_stage;

void main() {
    vec4 texel = texture(u_cracks, vec3(v_uv, float(u_stage)));
    if (texel.a < 0.01) {
        discard; // Nothing to blend; skip the write entirely.
    }
    fragColor = texel;
}
