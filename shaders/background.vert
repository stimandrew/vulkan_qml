#version 460

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Для фона используем только проекционную и видовую матрицы
    // Модельная матрица оставляем единичной
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
}
