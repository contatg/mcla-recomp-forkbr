#version 450

layout(location = 0) out vec2 uv;

void main() {
    const vec2 points[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0),
        vec2(-0.79, 0.79), vec2(-0.21, 0.79), vec2(-0.50, 0.19));
    vec2 position = points[gl_VertexIndex];
    uv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
