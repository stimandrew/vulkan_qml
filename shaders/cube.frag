#version 460

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Два направленных света
    vec3 lightDir1 = normalize(vec3(1.0, 1.0, -1.0));  // Основной свет
    vec3 lightDir2 = normalize(vec3(-0.5, -0.5, -1.0)); // Заполняющий свет
    vec3 lightColor1 = vec3(1.0, 1.0, 1.0);
    vec3 lightColor2 = vec3(0.8, 0.8, 1.0) * 0.5; // Немного голубоватый

    vec3 normal = normalize(fragNormal);

    // Ambient
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * lightColor1;

    // Основной направленный свет
    float diff1 = max(dot(normal, lightDir1), 0.0);
    vec3 diffuse1 = diff1 * lightColor1;

    // Заполняющий свет
    float diff2 = max(dot(normal, lightDir2), 0.0);
    vec3 diffuse2 = diff2 * lightColor2;

    // Комбинируем освещение
    vec3 result = (ambient + diffuse1 + diffuse2) * texColor.rgb;

    // Гамма-коррекция
    result = pow(result, vec3(1.0/2.2));

    outColor = vec4(result, 1.0);
}
