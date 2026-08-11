#version 460 core

in vec3 v_uvLayer;
in float v_shade;

out vec4 fragColor;

uniform sampler2DArray u_blockTextures;

void main() {
    // The block texture array is sRGB, so the sampler has already decoded to linear
    // and the shade multiply below is a light operation on light values. See
    // DESIGN.md 6.9.
    vec3 albedo = texture(u_blockTextures, v_uvLayer).rgb;
    fragColor = vec4(albedo * v_shade, 1.0);
}
