// Texture support proof for the babylon software renderer (RENDER_ONLY: no ctjs,
// no DOM). Decode a 2x2 PNG, sample it, then rasterize a textured quad and
// confirm each screen quadrant shows the matching texel colour. This is the
// unit-level backing for the glTF baseColor textures the Space-Invaders models
// (e.g. the blue player ship) rely on.
#define CTBROWSER_BABYLON_RENDER_ONLY
#include <ctbrowser/babylon.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace ctbrowser::babylon;

// 2x2 RGBA PNG: TL red, TR green, BL blue, BR white (generated with Pillow)
static const unsigned char PNG2x2[] = {
    137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 2, 0, 0, 0, 2, 8,
    6, 0, 0, 0, 114, 182, 13, 36, 0, 0, 0, 25, 73, 68, 65, 84, 120, 156, 5, 193, 1, 13, 0, 0,
    12, 195, 32, 150, 220, 191, 229, 30, 68, 210, 77, 194, 3, 62, 255, 6, 0, 133, 208, 147,
    156, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130};

static int R(uint32_t p) { return static_cast<int>((p >> 16) & 0xFFu); }
static int G(uint32_t p) { return static_cast<int>((p >> 8) & 0xFFu); }
static int B(uint32_t p) { return static_cast<int>(p & 0xFFu); }

static bool fail(const char * what) {
	std::printf("FAIL: %s\n", what);
	return false;
}

int main() {
	// --- 1) decode
	std::shared_ptr<r3d::texture> tex = r3d::decode_texture(PNG2x2, sizeof(PNG2x2));
	if (!tex || !tex->valid()) { return fail("PNG decode"), 1; }
	std::printf("decoded texture %dx%d\n", tex->w, tex->h);

	// --- 2) nearest-neighbour sampling of the four texels (v runs downward)
	const uint32_t tl = tex->sample(0.25, 0.25); // top-left  -> red
	const uint32_t tr = tex->sample(0.75, 0.25); // top-right -> green
	const uint32_t bl = tex->sample(0.25, 0.75); // bot-left  -> blue
	const uint32_t br = tex->sample(0.75, 0.75); // bot-right -> white
	if (!(R(tl) > 200 && G(tl) < 60 && B(tl) < 60)) { return fail("sample TL red"), 1; }
	if (!(G(tr) > 200 && R(tr) < 60 && B(tr) < 60)) { return fail("sample TR green"), 1; }
	if (!(B(bl) > 200 && R(bl) < 60 && G(bl) < 60)) { return fail("sample BL blue"), 1; }
	if (!(R(br) > 200 && G(br) > 200 && B(br) > 200)) { return fail("sample BR white"), 1; }
	std::printf("sampling OK (red/green/blue/white texels)\n");

	// --- 3) rasterize a textured quad filling the view (identity MVP: NDC == world)
	r3d::geo quad;
	quad.verts = {r3d::V3(-0.9, -0.9, 0.5), r3d::V3(0.9, -0.9, 0.5),
	              r3d::V3(0.9, 0.9, 0.5), r3d::V3(-0.9, 0.9, 0.5)};
	// screen Y is flipped, so the top verts (+Y) get UV v=0 (top of the texture)
	quad.uvs = {r3d::V2(0, 1), r3d::V2(1, 1), r3d::V2(1, 0), r3d::V2(0, 0)};
	quad.tris = {{0, 1, 2}, {0, 2, 3}};

	r3d::draw_item it;
	it.g = &quad;
	it.world = r3d::identity();
	it.tex = tex.get();
	it.cull = false; // don't fuss over winding in the unit test

	// two opposite hemispheric lights sum to a flat lit == 1 everywhere, so the
	// rasterized colour is the raw texel (no shading to reason about)
	std::vector<r3d::light> lights = {
	    r3d::light{0, r3d::V3(0, 0, 1), 1.0, r3d::rgba{1, 1, 1, 1}},
	    r3d::light{0, r3d::V3(0, 0, -1), 1.0, r3d::rgba{1, 1, 1, 1}}};

	const int W = 64, H = 64;
	std::vector<uint32_t> px(static_cast<size_t>(W * H), 0);
	r3d::view vw;
	vw.clear = r3d::rgba{0, 0, 0, 1};
	r3d::renderer rr;
	rr.render(px.data(), W, H, vw, {it}, lights);

	// sample one pixel from the middle of each screen quadrant
	auto at = [&](int x, int y) { return px[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)]; };
	const uint32_t q_tl = at(W / 4, H / 4);         // top-left  -> red
	const uint32_t q_tr = at(3 * W / 4, H / 4);     // top-right -> green
	const uint32_t q_bl = at(W / 4, 3 * H / 4);     // bot-left  -> blue
	const uint32_t q_br = at(3 * W / 4, 3 * H / 4); // bot-right -> white
	std::printf("quadrants: TL=%06X TR=%06X BL=%06X BR=%06X\n", q_tl & 0xFFFFFFu,
	            q_tr & 0xFFFFFFu, q_bl & 0xFFFFFFu, q_br & 0xFFFFFFu);
	if (!(R(q_tl) > G(q_tl) && R(q_tl) > B(q_tl))) { return fail("raster TL not red-dominant"), 1; }
	if (!(G(q_tr) > R(q_tr) && G(q_tr) > B(q_tr))) { return fail("raster TR not green-dominant"), 1; }
	if (!(B(q_bl) > R(q_bl) && B(q_bl) > G(q_bl))) { return fail("raster BL not blue-dominant"), 1; }
	if (!(R(q_br) > 150 && G(q_br) > 150 && B(q_br) > 150)) { return fail("raster BR not white"), 1; }

	std::printf("texture render: PASS (textured quad samples correctly)\n");
	return 0;
}
