#version 450

// Sample the tile. Blending is fixed-function state on the pipeline, matching
// the software backend's source-over, so the two produce the same image.
//
// set = 2, binding = 0 is where SDL3 puts fragment samplers for SPIR-V.

layout(set = 2, binding = 0) uniform sampler2D tile;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() { out_color = texture(tile, in_uv); }
