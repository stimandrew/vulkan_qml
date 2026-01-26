#version 460

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;

void main() {
    // Вычисляем позицию в мировом пространстве
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);

    // Преобразуем нормаль в мировое пространство
    // (используем только поворотную часть матрицы модели, без масштабирования)
    mat3 normalMatrix = mat3(ubo.model);
    normalMatrix = transpose(inverse(normalMatrix));
    fragNormal = normalize(normalMatrix * inNormal);

    // Передаем позицию во фрагментный шейдер для освещения
    fragPos = worldPos.xyz;

    gl_Position = ubo.proj * ubo.view * worldPos;
    fragTexCoord = inTexCoord;
}
