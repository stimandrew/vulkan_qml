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
    // Прямое преобразование в NDC пространство
    // Поскольку наши вершины уже в NDC (-1..1)
    gl_Position = vec4(inPosition, 1.0);
    
    // Передаем текстурные координаты
    fragTexCoord = inTexCoord;
}
