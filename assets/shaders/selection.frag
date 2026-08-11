#version 460 core

out vec4 fragColor;

// Near-black rather than pure black, and opaque. Minecraft's selection box is the
// same choice: a dark outline reads against every block in the palette, where a
// bright one disappears against sand and snow.
//
// Linear, not sRGB -- GL_FRAMEBUFFER_SRGB is enabled, so the hardware encodes on
// write and a value picked in sRGB would come out washed. See DESIGN.md 6.9.
void main() {
    fragColor = vec4(vec3(0.02), 1.0);
}
