#version 460 core

// The translucent pass. Diverges from chunk.frag in two ways: there is no ambient
// occlusion term, because those bits carry the surface height instead, and the
// surface moves.

in vec3 v_normal;
in vec3 v_worldPos;
in vec2 v_uv;
in float v_light;
flat in uint v_layer;
flat in vec2 v_flow;

layout(binding = 0) uniform sampler2DArray u_blockTextures;

uniform vec3 u_cameraPosition;
uniform float u_fadeDistance;
uniform vec3 u_fogColor;
/// Seconds since the engine started. The only animated thing in the renderer.
uniform float u_time;

out vec4 o_fragColor;

float srgbToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// Same fixed per-face brightness the opaque pass uses, so a water surface and a
// stone one next to it agree about which way is up.
float faceShading(vec3 normal) {
    if (normal.y > 0.5)  return srgbToLinear(1.00);
    if (normal.y < -0.5) return srgbToLinear(0.55);
    if (abs(normal.x) > 0.5) return srgbToLinear(0.75);
    return srgbToLinear(0.85);
}

void main() {
    // **Still water drifts; flowing water runs downhill.**
    //
    // The drift is what stops a lake reading as glass -- a surface that does not
    // move is not read as liquid whatever it is textured with, and this engine's
    // water texture is deliberately almost flat, so the motion has to carry it. The
    // two rates are close but not equal, so the pattern never repeats on a period a
    // player can see.
    //
    // The flow term is the surface gradient the vertex shader worked out from the
    // corner drops. It costs nothing to compute and it is the only thing on screen
    // that says which way a stream is going.
    // **Fast enough to see.** The first rate here was 0.037 blocks a second, which
    // moves the pattern one tile in half a minute -- chosen when the texture had no
    // pattern to move and nothing could have shown it was too slow. Vanilla cycles
    // its water animation in a few seconds; this is the same order.
    vec2 drift = vec2(u_time * 0.16, u_time * 0.125);
    vec2 flow = v_flow * u_time * 0.45;
    vec4 albedo = texture(u_blockTextures, vec3(v_uv + drift + flow, float(v_layer)));

    vec3 shaded = albedo.rgb * faceShading(v_normal) * v_light;

    // A slow swell across the surface, in world space so it does not swim when the
    // camera moves and does not break at a section boundary. Small on purpose: this
    // is the difference between a flat sheet and water, not a wave.
    float swell = 1.0 + 0.035 * sin(v_worldPos.x * 0.7 + v_worldPos.z * 0.5
                                    + u_time * 1.6);
    shaded *= swell;

    float distance = length(v_worldPos - u_cameraPosition);
    float fog = smoothstep(u_fadeDistance * 0.55, u_fadeDistance, distance);

    o_fragColor = vec4(mix(shaded, u_fogColor, fog), albedo.a);
}
