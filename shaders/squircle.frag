#version 450

layout(std140, binding = 0) uniform buf {
    float t;
} ubuf;

layout(location = 0) in vec2 vPosition;
layout(location = 0) out vec4 fragColor;

void main() {
    float distance = length(vPosition);
    vec3 color = vec3(1.0 - distance * 2.0, 0.5, 1.0) * 0.7;
    
    // Анимированный сквиркл
    float squircle = pow(abs(vPosition.x), 4.0) + pow(abs(vPosition.y), 4.0);
    float anim = sin(ubuf.t * 3.14159 * 2.0) * 0.5 + 0.5;
    float threshold = mix(0.7, 1.2, anim);
    
    if (squircle > threshold) {
        fragColor = vec4(color * 0.3, 1.0);
    } else {
        fragColor = vec4(color, 1.0);
    }
}
