#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(push_constant) uniform Draw { int triangle; } draw;

layout(set = 0, binding = 0) uniform usamplerBuffer pattern_texture;

void main() {
    if (draw.triangle == 2) {
        color = vec4(0.06, 0.22, 0.46, 1.0);
    } else if (draw.triangle == 1) {
        color = vec4(1.0, 0.78, 0.18, 1.0);
    } else if (uv.y < 0.5 && uv.x < 0.5) {
        int band = clamp(int(uv.x * 8.0), 0, 3);
        const vec3 bands[4] = vec3[4](vec3(0.95, 0.08, 0.12), vec3(0.12, 0.86, 0.23),
                                     vec3(0.10, 0.29, 0.95), vec3(1.0));
        color = vec4(bands[band], 1.0);
    } else if (uv.y < 0.5) {
        float t = (uv.x - 0.5) * 2.0;
        color = vec4(t, t, t, 1.0);
    } else if (uv.x < 0.5) {
        color = vec4(0.06, 0.16, 0.26, 1.0);
    } else {
        ivec2 texel = clamp(ivec2((uv - 0.5) * 16.0), ivec2(0), ivec2(7));
        bool light = texelFetch(pattern_texture, texel.y * 8 + texel.x).r != 0u;
        color = light ? vec4(0.15, 0.88, 0.84, 1.0) : vec4(0.08, 0.14, 0.23, 1.0);
    }
}
