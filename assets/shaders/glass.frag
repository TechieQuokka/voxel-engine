#version 460 core

// The cutout pass. Identical to chunk.frag but for one `discard`, and that one line
// is the whole reason this is a separate program rather than a branch in that one.
//
// **A fragment shader that *can* discard loses early-Z for every draw it is in**,
// whether or not the branch is taken -- the hardware cannot reject a fragment on
// depth before running a shader that might decide the fragment does not exist. Folding
// the test into chunk.frag would therefore pay for it on all of the terrain in the
// world in order to draw the handful of tiles that need it. Vanilla splits its render
// layers along the same line and calls this one `cutout`.
//
// It shares chunk.vert: the geometry, the packing and the lighting are the same, and
// only the fragment rule differs.

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
uniform vec3 u_fogColor;

out vec4 o_fragColor;

/// Below this the fragment does not exist. Half is the conventional choice and the
/// value barely matters here, because the glass tile's alpha is deliberately binary:
/// the frame is 255, the pane is 0, and the corner glint sits at 0x66 so that it
/// survives. See the `glass` layer in world/BlockTable.hpp.
const float kAlphaCutoff = 0.5;

float srgbToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float faceShading(vec3 normal) {
    if (normal.y > 0.5)  return srgbToLinear(1.00);   // top
    if (normal.y < -0.5) return srgbToLinear(0.55);   // bottom
    if (abs(normal.x) > 0.5) return srgbToLinear(0.75);
    return srgbToLinear(0.85);                        // +/- Z
}

void main() {
    vec4 albedo = texture(u_blockTextures, vec3(v_uv, float(v_layer)));

    // **First, before any of the shading work.** The point of an alpha test is not to
    // produce a transparent pixel, it is to not produce a pixel -- so a discarded
    // fragment must not pay for the lighting or the fog either.
    if (albedo.a < kAlphaCutoff) {
        discard;
    }

    float ao = mix(1.0, v_ao, u_aoStrength);
    vec3 shaded = albedo.rgb * faceShading(v_normal) * ao * v_light;

    float distance = length(v_worldPos - u_cameraPosition);
    float fog = smoothstep(u_fadeDistance * 0.55, u_fadeDistance, distance);

    // Alpha forced to 1 rather than passed through. Anything that survived the test
    // above is solid -- this pass does not blend, and the glint's 0x66 is a shading
    // hint rather than a transparency. Handing it to a non-blending pass as alpha
    // would simply be ignored, and writing it invites someone to turn blending on
    // later and get a different picture for no stated reason.
    o_fragColor = vec4(mix(shaded, u_fogColor, fog), 1.0);
}
