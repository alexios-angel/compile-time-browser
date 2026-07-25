#version 450

// One textured quad per tile. There is no vertex buffer: six vertices are
// generated from gl_VertexIndex, and the destination rectangle arrives as a
// uniform. A compositor draws thousands of these per frame and none of them
// have per-vertex data worth uploading.
//
// set = 1, binding = 0 is where SDL3 puts vertex uniform buffers for SPIR-V.

layout(set = 1, binding = 0) uniform Quad {
	vec4 dest; // x, y, width, height in normalised device coordinates
} quad;

layout(location = 0) out vec2 out_uv;

void main() {
	const vec2 corners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
	                                vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
	vec2 c = corners[gl_VertexIndex];
	// SDL_GPU's NDC has +Y UP (lower-left is -1,-1) while its texture
	// coordinates have +Y DOWN. So the quad corner that sits lowest on screen
	// samples the BOTTOM of the tile, and v has to be flipped against the
	// position. Getting this wrong flips every tile individually, which looks
	// like noise rather than like an upside-down page.
	out_uv = vec2(c.x, 1.0 - c.y);
	gl_Position = vec4(quad.dest.xy + c * quad.dest.zw, 0.0, 1.0);
}
