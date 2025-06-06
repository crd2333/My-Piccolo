#version 310 es

#extension GL_GOOGLE_include_directive : enable

#include "constants.h"

layout(input_attachment_index = 0, set = 0, binding = 0) uniform highp subpassInput in_color;

layout(location = 0) in highp vec2 in_uv;

layout(location = 0) out highp vec4 out_color;

void main()
{
    highp vec4 color       = subpassLoad(in_color).rgba;

    highp float cutoff = 0.4f;
    highp float exponent = 1.5f;
    highp float len = length(in_uv - 0.5f);
    highp float ratio = pow(cutoff / len, exponent);

    out_color = color * min(ratio, 1.0f);
}
