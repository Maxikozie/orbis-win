#version 450
layout(location = 0) in vec3 in_position;
layout(location = 0) out vec2 uv;
void main() {
    uint v = uint(gl_VertexIndex) % 3u;
    gl_Position = vec4(in_position, 1.0);
    uv = vec2(float(v == 1u), float(v == 2u));
}
