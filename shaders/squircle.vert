#version 450

layout(location = 0) in vec2 position;

layout(std140, binding = 0) uniform buf {
    float t;
} ubuf;

layout(location = 0) out vec2 vPosition;

void main() {
    vPosition = position;
    gl_Position = vec4(position, 0.0, 1.0);
}
