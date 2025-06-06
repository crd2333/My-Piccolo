#version 310 es

#extension GL_GOOGLE_include_directive : enable

#include "constants.h"

// 各种后处理效果通用的 vertex shader

void main()
{
    // 一个能够覆盖整个屏幕 [-1.0, 1.0] 的三角形
    const vec3 fullscreen_triangle_positions[3] = vec3[3](vec3(3.0, 1.0, 0.5), vec3(-1.0, 1.0, 0.5), vec3(-1.0, -3.0, 0.5));
    gl_Position = vec4(fullscreen_triangle_positions[gl_VertexIndex], 1.0);
}