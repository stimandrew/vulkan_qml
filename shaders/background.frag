#version 460

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 correctedTexCoord = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec4 texColor = texture(texSampler, correctedTexCoord);
    
    // Простой вывод текстуры фона без освещения
    outColor = vec4(texColor.rgb, 1.0);
}
