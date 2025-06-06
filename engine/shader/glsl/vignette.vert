#version 310 es

#extension GL_GOOGLE_include_directive : enable

#include "constants.h"

layout(location = 0) out vec2 out_uv;

void main()
{
    // 一个能够覆盖整个屏幕 [-1.0, 1.0] 的三角形
    const vec3 fullscreen_triangle_positions[3] = vec3[3](vec3(3.0, 1.0, 0.5), vec3(-1.0, 1.0, 0.5), vec3(-1.0, -3.0, 0.5));
    gl_Position = vec4(fullscreen_triangle_positions[gl_VertexIndex], 1.0);
    out_uv = vec2(fullscreen_triangle_positions[gl_VertexIndex].xy + 1.0f) * 0.5f;
}