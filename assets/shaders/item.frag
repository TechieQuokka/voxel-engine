#version 460 core

in vec3 v_uvLayer;
in float v_shade;

out vec4 fragColor;

uniform sampler2DArray u_blockTextures;

void main() {
    // The block texture array is sRGB, so the sampler has already decoded to linear
    // and the shade multiply below is a light operation on light values. See
    // DESIGN.md 6.9.
    vec4 albedo = texture(u_blockTextures, v_uvLayer);

    // A dropped stick or pickaxe is an icon layer with a transparent background, and
    // this cube is what carries it. Without the discard the empty part of the tile
    // draws as an opaque black box with a tool painted on one face.
    //
    // **A cube is the wrong shape for a tool and this is knowingly a placeholder.**
    // Vanilla draws dropped items as flat billboards, which needs a second geometry
    // path in ItemRenderer -- the same non-cube geometry Phase 10 builds for
    // vegetation. Discarding gets the silhouette right from four of six angles, which
    // is enough to tell a dropped pickaxe from a dropped cobblestone.
    if (albedo.a < 0.5) {
        discard;
    }
    fragColor = vec4(albedo.rgb * v_shade, 1.0);
}
