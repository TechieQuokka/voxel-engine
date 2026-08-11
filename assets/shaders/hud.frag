#version 460 core

in vec2 v_uv;
in vec4 v_tint;
flat in int v_mode;
flat in int v_layer;

out vec4 fragColor;

uniform sampler2DArray u_blockTextures;
uniform sampler2DArray u_glyphs;

// Matches HudRenderer::Mode.
const int kSolid = 0;
const int kBlock = 1;
const int kGlyph = 2;

void main() {
    if (v_mode == kSolid) {
        fragColor = v_tint;
        return;
    }

    if (v_mode == kBlock) {
        // The block array is sRGB, so this is already linear -- which is what the
        // tint multiply below wants. See DESIGN.md 6.9.
        vec3 albedo = texture(u_blockTextures, vec3(v_uv, float(v_layer))).rgb;
        fragColor = vec4(albedo * v_tint.rgb, v_tint.a);
        return;
    }

    // A glyph carries coverage in its alpha and nothing in its colour, so the tint
    // is the whole appearance. Discarding rather than blending zero keeps the
    // digits from laying down a faint box around themselves.
    float coverage = texture(u_glyphs, vec3(v_uv, float(v_layer))).a;
    if (coverage < 0.5) {
        discard;
    }
    fragColor = v_tint;
}
