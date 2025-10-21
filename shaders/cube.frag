#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // Два источника света
    vec3 lightPos1 = vec3(3.0, 3.0, 3.0);
    vec3 lightPos2 = vec3(-3.0, -3.0, 3.0);
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(-fragPos);
    
    // Ambient
    float ambientStrength = 0.4;
    vec3 ambient = ambientStrength * lightColor;
    
    // Первый источник света
    vec3 lightDir1 = normalize(lightPos1 - fragPos);
    float diff1 = max(dot(normal, lightDir1), 0.0);
    vec3 diffuse1 = diff1 * lightColor;
    
    // Второй источник света
    vec3 lightDir2 = normalize(lightPos2 - fragPos);
    float diff2 = max(dot(normal, lightDir2), 0.0);
    vec3 diffuse2 = diff2 * lightColor * 0.3; // Немного слабее
    
    // Комбинируем освещение
    vec3 result = (ambient + diffuse1 + diffuse2) * texColor.rgb;
    result = pow(result, vec3(0.9)); // Гамма-коррекция
    
    outColor = vec4(result, 1.0);
}
