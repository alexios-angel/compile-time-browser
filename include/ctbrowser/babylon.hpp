#ifndef CTBROWSER__BABYLON__HPP
#define CTBROWSER__BABYLON__HPP

// BabylonJS core API, on a software 3D rasterizer.
//
// This is a deliberate PER-LIBRARY SHIM (an explicit exception to the
// project's "no library-specific shims" rule): ctbrowser's canvas is a
// 2D software pixel buffer with no WebGL, so instead of implementing
// WebGL we implement the BABYLON.* surface directly, backed by a small
// software 3D renderer that rasterizes into the canvas's pixels.
//
// Supported CORE subset: BABYLON.Engine / Scene / ArcRotateCamera &
// FreeCamera / HemisphericLight & DirectionalLight / StandardMaterial /
// MeshBuilder.CreateBox|Sphere|Ground|Cylinder (+ legacy Mesh.Create*) /
// Vector3 / Color3 / Color4 / engine.runRenderLoop / scene.render, with
// flat/textured, z-buffered, perspective 3D and basic ArcRotate mouse
// orbit. glTF/GLB models load (SceneLoader.ImportMeshAsync) with their
// baseColor TEXTURES (PNG/JPEG, decoded at runtime and sampled with
// perspective-correct UVs). INTENTIONALLY OUT OF SCOPE: PBR lighting,
// physics, particles, post-processing, shadows, animations, GUI,
// Observables, WebGL/shader parity. Unknown APIs are simply undefined;
// a few commonly-probed ones are harmless no-ops.
//
// Math uses Boost.QVM (header-only). The renderer (namespace r3d) is
// pure C++ with no ctjs/DOM dependency and operates on a raw pixel span,
// so it is unit-testable in isolation; define CTBROWSER_BABYLON_RENDER_ONLY
// before including to get just the renderer (no bindings).

#ifndef CTBROWSER_BABYLON_RENDER_ONLY
#include "dom.hpp"
#include "script.hpp"
#include "utf.hpp"
#endif

#ifndef CTBROWSER_IN_A_MODULE
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>
#include <bit>
#define GLM_FORCE_CTOR_INIT     // default-construct vec/mat as zero
#define GLM_ENABLE_EXPERIMENTAL // for gtx/euler_angles (glm::yawPitchRoll)
#include <glm/glm.hpp>      // the vector/matrix types AND math (constexpr-capable)
#include <glm/gtc/matrix_transform.hpp> // glm::translate/scale/rotate/lookAtLH
#include <glm/ext/matrix_clip_space.hpp> // glm::perspectiveLH_ZO
#include <glm/gtx/euler_angles.hpp>      // glm::yawPitchRoll
#endif

// stb_image (public domain, Sean Barrett) - decodes glTF baseColor PNG/JPEG
// textures at RUNTIME into RGBA for the software sampler. STB_IMAGE_STATIC gives
// the loader internal linkage so it is safe to carry in the shared PCH; the
// vendored C header is wrapped so its style passes -Werror -Wconversion. This is
// runtime-only (never the constexpr renderer path).
#ifndef CTBROWSER_IN_A_MODULE
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
#pragma clang diagnostic pop
#endif

namespace ctbrowser::babylon {

// ============================================================monospace
//  r3d — the software 3D renderer (GLM math + z-buffered raster)
//  Pure C++: no ctjs, no DOM. Column-vector convention (clip = M * p),
//  LEFT-HANDED (Babylon default): +X right, +Y up, +Z into the screen.
//  Types are GLM's double vec/mat: vectors index as v[i], and matrices are
//  COLUMN-MAJOR - element (row r, col c) is m[c][r].
// ============================================================

namespace r3d {

using vec2 = glm::dvec2;
using vec3 = glm::dvec3;
using vec4 = glm::dvec4;
using mat4 = glm::dmat4;

// scalar math: GLM at runtime, a constexpr implementation at compile time (GLM's
// sqrt/floor/ceil are not constexpr on this toolchain, so they get an `if
// consteval` fallback; glm::abs IS constexpr and is used directly).
constexpr double ct_sqrt(double x) noexcept { // Newton-Raphson (compile-time only)
	if (!(x > 0.0)) { return 0.0; }
	double g = x, prev = 0.0;
	for (int i = 0; i < 64 && g != prev; ++i) { prev = g; g = 0.5 * (g + x / g); }
	return g;
}
constexpr double ct_floor(double x) noexcept {
	const long long i = static_cast<long long>(x);
	return static_cast<double>(x < static_cast<double>(i) ? i - 1 : i);
}
constexpr double ct_ceil(double x) noexcept {
	const long long i = static_cast<long long>(x);
	return static_cast<double>(x > static_cast<double>(i) ? i + 1 : i);
}
constexpr double fsqrt(double x) noexcept {
	if consteval { return ct_sqrt(x); } else { return glm::sqrt(x); }
}
constexpr double ffloor(double x) noexcept {
	if consteval { return ct_floor(x); } else { return glm::floor(x); }
}
constexpr double fceil(double x) noexcept {
	if consteval { return ct_ceil(x); } else { return glm::ceil(x); }
}

constexpr vec3 V3(double x, double y, double z) noexcept { return vec3(x, y, z); }
constexpr vec2 V2(double x, double y) noexcept { return vec2(x, y); }

// --- vector helpers via GLM. GLM's construction/+/-/dot/cross and matrix
// products are constexpr on this toolchain, so they fold in the compile-time
// renderer; normalize/length are NOT, so norm3 keeps an `if consteval` constexpr
// path (best of both worlds - GLM at runtime, the ccmath path at compile time).
constexpr vec3 sub(const vec3 & a, const vec3 & b) noexcept { return a - b; }
constexpr double dot3(const vec3 & a, const vec3 & b) noexcept { return glm::dot(a, b); }
constexpr vec3 cross3(const vec3 & a, const vec3 & b) noexcept { return glm::cross(a, b); }
constexpr double mag3(const vec3 & a) noexcept { return fsqrt(dot3(a, a)); }
constexpr vec3 norm3(const vec3 & a) noexcept {
	if consteval { // glm::normalize is not constexpr; use the ccmath path
		const double m = mag3(a);
		return m > 1e-12 ? vec3(a[0] / m, a[1] / m, a[2] / m) : vec3(0, 0, 0);
	} else { // runtime: GLM
		return glm::length(a) > 1e-12 ? glm::normalize(a) : vec3(0, 0, 0);
	}
}

// --- colors
struct rgba { double r = 1, g = 1, b = 1, a = 1; };

constexpr uint32_t pack(const rgba & c, double lit) noexcept {
	auto ch = [&](double v) -> uint32_t {
		double s = v * lit * 255.0;
		s = s < 0 ? 0 : (s > 255 ? 255 : s);
		return static_cast<uint32_t>(s + 0.5);
	};
	double av = c.a < 0 ? 0 : (c.a > 1 ? 1 : c.a);
	uint32_t A = static_cast<uint32_t>(av * 255.0 + 0.5);
	return (A << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

// --- fast CONSTEXPR trig. Inspired by a lookup-table technique (Kevin P.
// Rice, StackOverflow): a SINGLE per-degree cos table drives BOTH cos and
// sin - since sin x = cos(90-x) - with quadrant symmetry and linear
// interpolation. Accurate to ~1e-4, and unlike std::sin/std::cos/std::tan
// (not constexpr until C++26) it evaluates at COMPILE TIME. Angles: radians.
inline constexpr int kCosTable[91] = {
    64000, 63990, 63961, 63912, 63844, 63756, 63649, 63523, 63377, 63212,
    63028, 62824, 62601, 62360, 62099, 61819, 61521, 61204, 60868, 60513,
    60140, 59749, 59340, 58912, 58467, 58004, 57523, 57024, 56509, 55976,
    55426, 54859, 54275, 53675, 53058, 52426, 51777, 51113, 50433, 49737,
    49027, 48301, 47561, 46807, 46038, 45255, 44458, 43648, 42824, 41988,
    41138, 40277, 39402, 38516, 37618, 36709, 35788, 34857, 33915, 32962,
    32000, 31028, 30046, 29055, 28056, 27048, 26031, 25007, 23975, 22936,
    21889, 20836, 19777, 18712, 17641, 16564, 15483, 14397, 13306, 12212,
    11113, 10012,  8907,  7800,  6690,  5578,  4464,  3350,  2234,  1117,
        0};

// cos of x degrees, x in [0,90], interpolated between table samples
constexpr double cos_deg01(double x) noexcept {
	if (x <= 0.0) { return 1.0; }
	if (x >= 90.0) { return 0.0; }
	const int i = static_cast<int>(x);
	const double frac = x - static_cast<double>(i);
	const double a = static_cast<double>(kCosTable[i]);
	const double b = static_cast<double>(kCosTable[i + 1]);
	return (a + (b - a) * frac) * 0.000015625; // * 1/64000
}

// sin AND cos of a radian angle, together (the whole point of the table)
constexpr void fsincos(double rad, double & s, double & c) noexcept {
	double deg = rad * 57.295779513082320876; // 180/pi
	const long k = static_cast<long>(deg * (1.0 / 360.0));
	deg -= 360.0 * static_cast<double>(k);
	if (deg < 0.0) { deg += 360.0; }               // now in [0, 360)
	const int quad = static_cast<int>(deg * (1.0 / 90.0)) & 3;
	const double d = deg - 90.0 * static_cast<double>(quad); // [0, 90)
	const double cd = cos_deg01(d);                // cos(d)
	const double sd = cos_deg01(90.0 - d);         // sin(d) = cos(90-d)
	switch (quad) {
		case 0: c = cd;  s = sd;  break;
		case 1: c = -sd; s = cd;  break;            // 90 + d
		case 2: c = -cd; s = -sd; break;            // 180 + d
		default: c = sd; s = -cd; break;            // 270 + d
	}
}
// At runtime use GLM's full-precision trig; at compile time (where std/glm trig
// is not constexpr) fall back to the fast lookup-table above.
constexpr double fcos(double rad) noexcept {
	if consteval { double s = 0, c = 0; fsincos(rad, s, c); return c; }
	else { return glm::cos(rad); }
}
constexpr double fsin(double rad) noexcept {
	if consteval { double s = 0, c = 0; fsincos(rad, s, c); return s; }
	else { return glm::sin(rad); }
}
constexpr double ftan(double rad) noexcept {
	if consteval {
		double s = 0, c = 0;
		fsincos(rad, s, c);
		return c != 0.0 ? s / c : 0.0;
	} else {
		return glm::tan(rad);
	}
}

// --- matrix builders (explicit element fills, column-vector M*p)
constexpr mat4 identity() noexcept { return mat4(1.0); } // GLM identity
constexpr mat4 translation(double x, double y, double z) noexcept {
	return glm::translate(mat4(1.0), vec3(x, y, z)); // GLM (constexpr on this clang)
}
constexpr mat4 scaling(double x, double y, double z) noexcept {
	return glm::scale(mat4(1.0), vec3(x, y, z)); // GLM (constexpr on this clang)
}
// rotations: glm::rotate at runtime (not constexpr), the fast-table fill at
// compile time. Both conventions agree (verified in tests/babylon.cpp).
constexpr mat4 rotationX(double t) noexcept {
	if consteval {
		mat4 m = identity();
		const double c = fcos(t), s = fsin(t);
		m[1][1] = c; m[2][1] = -s; m[1][2] = s; m[2][2] = c;
		return m;
	} else {
		return glm::rotate(mat4(1.0), t, vec3(1, 0, 0));
	}
}
constexpr mat4 rotationY(double t) noexcept {
	if consteval {
		mat4 m = identity();
		const double c = fcos(t), s = fsin(t);
		m[0][0] = c; m[2][0] = s; m[0][2] = -s; m[2][2] = c;
		return m;
	} else {
		return glm::rotate(mat4(1.0), t, vec3(0, 1, 0));
	}
}
constexpr mat4 rotationZ(double t) noexcept {
	if consteval {
		mat4 m = identity();
		const double c = fcos(t), s = fsin(t);
		m[0][0] = c; m[1][0] = -s; m[0][1] = s; m[1][1] = c;
		return m;
	} else {
		return glm::rotate(mat4(1.0), t, vec3(0, 0, 1));
	}
}
constexpr mat4 matmul(const mat4 & a, const mat4 & b) noexcept { return a * b; } // GLM product
// Babylon mesh.rotation Vector3 (rx,ry,rz) = yaw-pitch-roll, applied YXZ
constexpr mat4 rotationYPR(double rx, double ry, double rz) noexcept {
	if consteval {
		return matmul(rotationY(ry), matmul(rotationX(rx), rotationZ(rz)));
	} else { // yaw = Y(ry), pitch = X(rx), roll = Z(rz)
		return glm::yawPitchRoll(ry, rx, rz);
	}
}
constexpr mat4 mul(const mat4 & a, const mat4 & b) noexcept { return matmul(a, b); }

// left-handed perspective (column-vector: clip = P * viewPos)
constexpr mat4 perspectiveFovLH(double fov, double aspect, double zn, double zf) noexcept {
	if consteval {
		mat4 m;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c) m[c][r] = 0.0;
		const double f = 1.0 / ftan(fov * 0.5);
		m[0][0] = f / aspect;
		m[1][1] = f;
		m[2][2] = zf / (zf - zn);
		m[3][2] = -zn * zf / (zf - zn);
		m[2][3] = 1.0; // w = z (LH)
		return m;
	} else { // left-handed, [0,1] depth (matches the fill above)
		return glm::perspectiveLH_ZO(fov, aspect, zn, zf);
	}
}
// left-handed lookAt (column-vector view matrix)
constexpr mat4 lookAtLH(const vec3 & eye, const vec3 & target, const vec3 & up) noexcept {
	if consteval {
		const vec3 z = norm3(sub(target, eye));       // forward, +Z into screen
		const vec3 x = norm3(cross3(up, z));          // right
		const vec3 y = cross3(z, x);                   // true up
		mat4 m = identity();
		m[0][0] = x[0]; m[1][0] = x[1]; m[2][0] = x[2]; m[3][0] = -dot3(x, eye);
		m[0][1] = y[0]; m[1][1] = y[1]; m[2][1] = y[2]; m[3][1] = -dot3(y, eye);
		m[0][2] = z[0]; m[1][2] = z[1]; m[2][2] = z[2]; m[3][2] = -dot3(z, eye);
		m[0][3] = 0; m[1][3] = 0; m[2][3] = 0; m[3][3] = 1;
		return m;
	} else {
		return glm::lookAtLH(eye, target, up);
	}
}

// transform a point (w=1); returns the full vec4 (keep w for the divide)
constexpr vec4 xform(const mat4 & m, const vec3 & p) noexcept { return m * vec4(p, 1.0); }

// --- geometry
struct geo {
	std::vector<vec3> verts;
	std::vector<std::array<int, 3>> tris;
	std::vector<vec2> uvs; // parallel to verts; empty => untextured (flat colour)
};

// --- a decoded RGBA texture: 0xAARRGGBB texels (same packing as the canvas),
// row-major, origin top-left. Built at runtime from PNG/JPEG bytes; the
// rasterizer samples it via perspective-correct per-vertex UVs.
struct texture {
	int w = 0, h = 0;
	std::vector<uint32_t> texel;
	constexpr bool valid() const noexcept { return w > 0 && h > 0 && !texel.empty(); }
	// nearest-neighbour sample, UVs wrapped into [0,1) (v runs downward, glTF-style)
	constexpr uint32_t sample(double u, double v) const noexcept {
		if (!valid()) { return 0xFFFFFFFFu; }
		const double uu = u - ffloor(u), vv = v - ffloor(v);
		int tx = static_cast<int>(uu * w), ty = static_cast<int>(vv * h);
		tx = tx < 0 ? 0 : (tx >= w ? w - 1 : tx);
		ty = ty < 0 ? 0 : (ty >= h ? h - 1 : ty);
		return texel[static_cast<size_t>(ty) * static_cast<size_t>(w) + static_cast<size_t>(tx)];
	}
};

#ifndef CTBROWSER_IN_A_MODULE
// decode encoded image bytes (PNG/JPEG) into a shared texture; null on failure.
// RUNTIME ONLY (calls stb_image) - never reached during constexpr evaluation.
inline std::shared_ptr<texture> decode_texture(const unsigned char * data, size_t len) {
	if (data == nullptr || len == 0) { return nullptr; }
	int tw = 0, th = 0, comp = 0;
	unsigned char * p = stbi_load_from_memory(data, static_cast<int>(len), &tw, &th, &comp, 4);
	if (p == nullptr || tw <= 0 || th <= 0) {
		if (p != nullptr) { stbi_image_free(p); }
		return nullptr;
	}
	auto t = std::make_shared<texture>();
	t->w = tw;
	t->h = th;
	const size_t n = static_cast<size_t>(tw) * static_cast<size_t>(th);
	t->texel.resize(n);
	for (size_t i = 0; i < n; ++i) {
		const uint32_t r = p[i * 4 + 0], g = p[i * 4 + 1], b = p[i * 4 + 2], a = p[i * 4 + 3];
		t->texel[i] = (a << 24) | (r << 16) | (g << 8) | b;
	}
	stbi_image_free(p);
	return t;
}
#endif

constexpr geo make_box(double size) {
	const double h = size * 0.5;
	geo g;
	g.verts = { V3(-h,-h,-h), V3(h,-h,-h), V3(h,h,-h), V3(-h,h,-h),
	            V3(-h,-h, h), V3(h,-h, h), V3(h,h, h), V3(-h,h, h) };
	const int f[6][4] = { {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0} };
	// wound so each face normal points OUTWARD (else backface culling
	// keeps the interior faces and the cube renders inside-out)
	for (auto & q : f) {
		g.tris.push_back({q[0], q[2], q[1]});
		g.tris.push_back({q[0], q[3], q[2]});
	}
	return g;
}

constexpr geo make_sphere(double diameter, int segments) {
	const double rad = diameter * 0.5;
	const int seg = segments < 3 ? 3 : (segments > 24 ? 24 : segments);
	const int rings = seg, sectors = seg * 2;
	geo g;
	for (int i = 0; i <= rings; ++i) {
		const double phi = std::numbers::pi * (double(i) / rings);       // 0..pi
		for (int j = 0; j <= sectors; ++j) {
			const double theta = 2.0 * std::numbers::pi * (double(j) / sectors);
			g.verts.push_back(V3(rad * fsin(phi) * fcos(theta),
			                     rad * fcos(phi),
			                     rad * fsin(phi) * fsin(theta)));
		}
	}
	const int stride = sectors + 1;
	for (int i = 0; i < rings; ++i) {
		for (int j = 0; j < sectors; ++j) {
			const int a = i * stride + j, b = a + stride;
			g.tris.push_back({a, b, a + 1});
			g.tris.push_back({a + 1, b, b + 1});
		}
	}
	return g;
}

constexpr geo make_ground(double width, double height) {
	const double x = width * 0.5, z = height * 0.5;
	geo g;
	g.verts = { V3(-x, 0, -z), V3(x, 0, -z), V3(x, 0, z), V3(-x, 0, z) };
	g.tris = { {0, 2, 1}, {0, 3, 2} }; // wound so the face normal points +Y (up)
	return g;
}

constexpr geo make_cylinder(double height, double diameter, int tess) {
	const double r = diameter * 0.5, hh = height * 0.5;
	const int n = tess < 3 ? 3 : (tess > 48 ? 48 : tess);
	geo g;
	const int top0 = 0, bot0 = n; // ring vertices
	for (int i = 0; i < n; ++i) {
		const double a = 2.0 * std::numbers::pi * (double(i) / n);
		g.verts.push_back(V3(r * fcos(a), hh, r * fsin(a)));
	}
	for (int i = 0; i < n; ++i) {
		const double a = 2.0 * std::numbers::pi * (double(i) / n);
		g.verts.push_back(V3(r * fcos(a), -hh, r * fsin(a)));
	}
	const int topC = static_cast<int>(g.verts.size()); g.verts.push_back(V3(0, hh, 0));
	const int botC = static_cast<int>(g.verts.size()); g.verts.push_back(V3(0, -hh, 0));
	for (int i = 0; i < n; ++i) {
		const int j = (i + 1) % n;
		g.tris.push_back({top0 + i, bot0 + i, top0 + j});   // side
		g.tris.push_back({top0 + j, bot0 + i, bot0 + j});
		g.tris.push_back({topC, top0 + j, top0 + i});        // top cap
		g.tris.push_back({botC, bot0 + i, bot0 + j});        // bottom cap
	}
	return g;
}

// --- the scene as the renderer sees it (world-agnostic)
struct draw_item {
	const geo * g = nullptr;
	mat4 world = identity();
	rgba diffuse{1, 1, 1, 1};
	bool cull = true;
	const texture * tex = nullptr; // null => flat `diffuse`; else sampled per pixel
};
struct light {
	int type = 0;              // 0 = hemispheric, 1 = directional
	vec3 direction{};
	double intensity = 1.0;
	rgba diffuse{1, 1, 1, 1};
};
struct view {
	mat4 vp_view = identity();
	mat4 vp_proj = identity();
	rgba clear{0.2, 0.2, 0.3, 1.0};
};

class renderer {
public:
	// rasterize into a raw ARGB8888 span (row-major, stride = w)
	constexpr void render(uint32_t * px, int w, int h, const view & vw,
	            const std::vector<draw_item> & items,
	            const std::vector<light> & lights) {
		if (px == nullptr || w <= 0 || h <= 0) { return; }
		const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
		const uint32_t clear = pack(vw.clear, 1.0);
		for (size_t i = 0; i < n; ++i) { px[i] = clear; }
		zbuf_.assign(n, std::numeric_limits<double>::infinity());

		for (const draw_item & it : items) {
			if (it.g == nullptr) { continue; }
			const mat4 mvp = matmul(vw.vp_proj, matmul(vw.vp_view, it.world));
			for (const auto & tri : it.g->tris) {
				raster_tri(px, w, h, it, mvp, tri, lights);
			}
		}
	}

private:
	std::vector<double> zbuf_;

	constexpr double shade(const vec3 & N, const std::vector<light> & lights) const {
		double lit = 0.0;
		for (const light & L : lights) {
			const vec3 d = norm3(L.direction);
			if (L.type == 1) { // directional: light travels along direction
				lit += L.intensity * std::max(0.0, dot3(N, V3(-d[0], -d[1], -d[2])));
			} else {           // hemispheric: soft sky/ground term about `direction`
				lit += L.intensity * (dot3(N, d) * 0.5 + 0.5);
			}
		}
		return lit < 0 ? 0 : (lit > 1 ? 1 : lit);
	}

	constexpr void raster_tri(uint32_t * px, int w, int h, const draw_item & it, const mat4 & mvp,
	                const std::array<int, 3> & tri, const std::vector<light> & lights) {
		const vec3 & p0 = it.g->verts[static_cast<size_t>(tri[0])];
		const vec3 & p1 = it.g->verts[static_cast<size_t>(tri[1])];
		const vec3 & p2 = it.g->verts[static_cast<size_t>(tri[2])];

		// world-space positions (for the face normal) and clip positions
		const vec4 w0 = xform(it.world, p0), w1 = xform(it.world, p1), w2 = xform(it.world, p2);
		const vec4 c0 = mvp_point(mvp, p0), c1 = mvp_point(mvp, p1), c2 = mvp_point(mvp, p2);
		const double eps = 1e-6;
		if (c0[3] <= eps || c1[3] <= eps || c2[3] <= eps) { return; } // near-plane guard

		// perspective-correct texture setup: per-vertex 1/w and the triangle's UVs
		const bool textured = it.tex != nullptr && it.tex->valid() &&
		                      it.g->uvs.size() == it.g->verts.size();
		const double iw0 = 1.0 / c0[3], iw1 = 1.0 / c1[3], iw2 = 1.0 / c2[3];
		vec2 uv0{}, uv1{}, uv2{};
		if (textured) {
			uv0 = it.g->uvs[static_cast<size_t>(tri[0])];
			uv1 = it.g->uvs[static_cast<size_t>(tri[1])];
			uv2 = it.g->uvs[static_cast<size_t>(tri[2])];
		}

		// screen coords + depth
		const double W = w, H = h;
		auto scr = [&](const vec4 & c, double & sx, double & sy, double & sz) {
			const double iw = 1.0 / c[3];
			sx = (c[0] * iw * 0.5 + 0.5) * W;
			sy = (1.0 - (c[1] * iw * 0.5 + 0.5)) * H;
			sz = c[2] * iw;
		};
		double x0, y0, z0, x1, y1, z1, x2, y2, z2;
		scr(c0, x0, y0, z0); scr(c1, x1, y1, z1); scr(c2, x2, y2, z2);

		const double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
		if (glm::abs(area) < 1e-9) { return; }             // degenerate
		if (it.cull && area <= 0) { return; }                // backface (CW in screen space)

		const vec3 N = norm3(cross3(sub(V3(w1[0], w1[1], w1[2]), V3(w0[0], w0[1], w0[2])),
		                            sub(V3(w2[0], w2[1], w2[2]), V3(w0[0], w0[1], w0[2]))));
		// front faces have the normal facing roughly toward the camera for
		// our winding; flip so lighting uses the visible side
		const double lit = shade(area > 0 ? N : V3(-N[0], -N[1], -N[2]), lights);
		const uint32_t color = pack(it.diffuse, lit);

		int minx = static_cast<int>(ffloor(std::min({x0, x1, x2})));
		int maxx = static_cast<int>(fceil(std::max({x0, x1, x2})));
		int miny = static_cast<int>(ffloor(std::min({y0, y1, y2})));
		int maxy = static_cast<int>(fceil(std::max({y0, y1, y2})));
		minx = std::max(minx, 0); miny = std::max(miny, 0);
		maxx = std::min(maxx, w - 1); maxy = std::min(maxy, h - 1);
		const double inv = 1.0 / area;

		for (int py = miny; py <= maxy; ++py) {
			for (int pxi = minx; pxi <= maxx; ++pxi) {
				const double fx = pxi + 0.5, fy = py + 0.5;
				double b0 = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) * inv;
				double b1 = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) * inv;
				double b2 = 1.0 - b0 - b1;
				if (b0 < 0 || b1 < 0 || b2 < 0) { continue; }
				const double depth = b0 * z0 + b1 * z1 + b2 * z2;
				if (depth < 0 || depth > 1) { continue; }
				const size_t idx = static_cast<size_t>(py) * static_cast<size_t>(w) + static_cast<size_t>(pxi);
				if (depth >= zbuf_[idx]) { continue; }
				uint32_t out = color;
				if (textured) {
					// perspective-correct barycentric interpolation of the UVs
					const double wa = b0 * iw0, wb = b1 * iw1, wc = b2 * iw2;
					const double isum = 1.0 / (wa + wb + wc);
					const double u = (wa * uv0[0] + wb * uv1[0] + wc * uv2[0]) * isum;
					const double v = (wa * uv0[1] + wb * uv1[1] + wc * uv2[1]) * isum;
					const uint32_t t = it.tex->sample(u, v);
					if (((t >> 24) & 0xFFu) < 8u) { continue; } // alpha-tested texel: skip
					const rgba tc{it.diffuse.r * static_cast<double>((t >> 16) & 0xFFu) / 255.0,
					              it.diffuse.g * static_cast<double>((t >> 8) & 0xFFu) / 255.0,
					              it.diffuse.b * static_cast<double>(t & 0xFFu) / 255.0, 1.0};
					out = pack(tc, lit);
				}
				zbuf_[idx] = depth;
				px[idx] = out;
			}
		}
	}

	static constexpr vec4 mvp_point(const mat4 & mvp, const vec3 & p) noexcept { return xform(mvp, p); }
};

} // namespace r3d

// ============================================================
//  gltf — a minimal binary-glTF (GLB) loader. Pure C++ (+ r3d), no
//  ctjs. Parses the GLB container, a small JSON subset, and triangle
//  primitives (POSITION + indices), baking each node's transform into
//  world-space geometry. PBR baseColorFactor -> flat diffuse (no
//  textures / PBR / IBL; no nested node hierarchy).
// ============================================================
namespace gltf {

// --- a tiny JSON value (enough to navigate a glTF document)
struct jval;
using jarr = std::vector<jval>;
using jobj = std::vector<std::pair<std::string, jval>>;
struct jval {
	enum kind { nul, boolean, number, string, array, object } k = nul;
	double num = 0;
	bool boo = false;
	std::string str;
	std::unique_ptr<jarr> arr;   // unique_ptr is constexpr (shared_ptr is not)
	std::unique_ptr<jobj> obj;

	// move-only; the destructor is defined OUT OF LINE (below) where the
	// recursive jarr/jobj vector element type is complete - otherwise the
	// unique_ptr member destructors can't be instantiated
	constexpr jval() = default;
	constexpr jval(jval &&) = default;
	constexpr jval & operator=(jval &&) = default;
	jval(const jval &) = delete;
	jval & operator=(const jval &) = delete;
	constexpr ~jval();

	constexpr const jval * get(std::string_view key) const {
		if (k != object || !obj) { return nullptr; }
		for (const auto & kv : *obj) { if (kv.first == key) { return &kv.second; } }
		return nullptr;
	}
	// unchecked: callers index only valid array positions (sizes guarded)
	constexpr const jval & operator[](size_t i) const { return (*arr)[i]; }
	constexpr size_t size() const {
		return k == array && arr ? arr->size() : (k == object && obj ? obj->size() : 0);
	}
	constexpr double as_num(double d = 0) const { return k == number ? num : d; }
	constexpr int as_int(int d = 0) const { return k == number ? static_cast<int>(num) : d; }
	constexpr std::string as_str() const { return k == string ? str : std::string{}; }
};
// jarr (vector<jval>) and jobj are complete here -> the unique_ptr member
// destructors can be instantiated
constexpr jval::~jval() = default;

// constexpr helpers (std::strtod/memcpy/memcmp/to_string aren't constexpr)
constexpr double parse_double(const char * s, const char * e) noexcept {
	double sign = 1.0;
	if (s < e && *s == '-') { sign = -1.0; ++s; } else if (s < e && *s == '+') { ++s; }
	double val = 0.0;
	while (s < e && *s >= '0' && *s <= '9') { val = val * 10.0 + (*s - '0'); ++s; }
	if (s < e && *s == '.') {
		++s;
		double f = 0.1;
		while (s < e && *s >= '0' && *s <= '9') { val += (*s - '0') * f; f *= 0.1; ++s; }
	}
	if (s < e && (*s == 'e' || *s == 'E')) {
		++s;
		int esign = 1;
		if (s < e && *s == '-') { esign = -1; ++s; } else if (s < e && *s == '+') { ++s; }
		int ex = 0;
		while (s < e && *s >= '0' && *s <= '9') { ex = ex * 10 + (*s - '0'); ++s; }
		double pw = 1.0;
		for (int i = 0; i < ex; ++i) { pw *= 10.0; }
		val = esign > 0 ? val * pw : val / pw;
	}
	return sign * val;
}
constexpr uint32_t read_u32le(const unsigned char * p) noexcept {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
constexpr float read_f32le(const unsigned char * p) noexcept { return std::bit_cast<float>(read_u32le(p)); }
constexpr std::string cstr_int(size_t v) {
	if (v == 0) { return "0"; }
	std::string r;
	while (v != 0) { r += static_cast<char>('0' + v % 10); v /= 10; }
	for (size_t i = 0, j = r.size() - 1; i < j; ++i, --j) { const char t = r[i]; r[i] = r[j]; r[j] = t; }
	return r;
}

struct jparser {
	const char * p;
	const char * e;
	constexpr void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) { ++p; } }
	constexpr jval value() {
		ws();
		if (p >= e) { return {}; }
		const char c = *p;
		if (c == '{') { return object(); }
		if (c == '[') { return array(); }
		if (c == '"') { jval v; v.k = jval::string; v.str = str(); return v; }
		if (c == 't') { p += 4; jval v; v.k = jval::boolean; v.boo = true; return v; }
		if (c == 'f') { p += 5; jval v; v.k = jval::boolean; v.boo = false; return v; }
		if (c == 'n') { p += 4; return {}; }
		return number();
	}
	constexpr std::string str() {
		std::string s;
		if (p < e) { ++p; } // opening quote
		while (p < e && *p != '"') {
			char c = *p++;
			if (c == '\\' && p < e) {
				const char x = *p++;
				switch (x) {
					case 'n': s += '\n'; break;
					case 't': s += '\t'; break;
					case 'r': s += '\r'; break;
					case 'b': s += '\b'; break;
					case 'f': s += '\f'; break;
					case 'u': {
						unsigned cp = 0;
						for (int i = 0; i < 4 && p < e; ++i) {
							const char h = *p++;
							cp = cp * 16 + static_cast<unsigned>(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
						}
						if (cp < 0x80) { s += static_cast<char>(cp); }
						else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
						else { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
						break;
					}
					default: s += x;
				}
			} else {
				s += c;
			}
		}
		if (p < e) { ++p; } // closing quote
		return s;
	}
	constexpr jval number() {
		const char * s = p;
		while (p < e && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' ||
		                 (*p >= '0' && *p <= '9'))) { ++p; }
		jval v;
		v.k = jval::number;
		v.num = parse_double(s, p);
		return v;
	}
	constexpr jval array() {
		jval v; v.k = jval::array; v.arr = std::make_unique<jarr>(); ++p; ws();
		if (p < e && *p == ']') { ++p; return v; }
		while (p < e) {
			v.arr->push_back(value()); ws();
			if (p < e && *p == ',') { ++p; continue; }
			if (p < e && *p == ']') { ++p; break; }
			break;
		}
		return v;
	}
	constexpr jval object() {
		jval v; v.k = jval::object; v.obj = std::make_unique<jobj>(); ++p; ws();
		if (p < e && *p == '}') { ++p; return v; }
		while (p < e) {
			ws();
			std::string key = str();
			ws();
			if (p < e && *p == ':') { ++p; }
			jval val = value();
			v.obj->emplace_back(std::move(key), std::move(val));
			ws();
			if (p < e && *p == ',') { ++p; continue; }
			if (p < e && *p == '}') { ++p; break; }
			break;
		}
		return v;
	}
};
constexpr jval json_parse(std::string_view s) {
	jparser jp{s.data(), s.data() + s.size()};
	jp.ws();
	return jp.value();
}

// --- the parsed model
// base defaults to WHITE (the glTF baseColorFactor default) so a baseColor
// TEXTURE is sampled untinted; base_tex indexes model.textures (-1 = none).
struct material { std::string name; r3d::rgba base{1, 1, 1, 1}; int base_tex = -1; };
struct primitive {
	std::vector<r3d::vec3> verts;
	std::vector<std::array<int, 3>> tris;
	std::vector<r3d::vec2> uvs; // TEXCOORD_0, parallel to verts (empty = none)
	int material = -1;
	std::string node_name;
};
struct model {
	std::vector<primitive> prims;
	std::vector<material> materials;
	// encoded image bytes per glTF texture index (PNG/JPEG), decoded at runtime;
	// parse stays constexpr - only the byte ranges are copied here, never decoded
	std::vector<std::vector<unsigned char>> textures;
	r3d::vec3 bmin{}, bmax{};
	bool ok = false;
};

// node "matrix" is column-major (glTF spec) -> our a[row][col]
constexpr r3d::mat4 node_matrix(const jval & node) {
	if (const jval * m = node.get("matrix")) {
		if (m->size() == 16) {
			r3d::mat4 M = r3d::identity();
			for (int c = 0; c < 4; ++c)
				for (int r = 0; r < 4; ++r) M[c][r] = (*m)[static_cast<size_t>(c * 4 + r)].as_num();
			return M;
		}
	}
	r3d::mat4 T = r3d::identity(), R = r3d::identity(), S = r3d::identity();
	if (const jval * t = node.get("translation"); t != nullptr && t->size() >= 3)
		T = r3d::translation((*t)[0].as_num(), (*t)[1].as_num(), (*t)[2].as_num());
	if (const jval * s = node.get("scale"); s != nullptr && s->size() >= 3)
		S = r3d::scaling((*s)[0].as_num(1), (*s)[1].as_num(1), (*s)[2].as_num(1));
	if (const jval * q = node.get("rotation"); q != nullptr && q->size() >= 4) {
		// quaternion (x,y,z,w) -> rotation matrix
		const double x = (*q)[0].as_num(), y = (*q)[1].as_num(), z = (*q)[2].as_num(), w = (*q)[3].as_num(1);
		R[0][0] = 1 - 2 * (y * y + z * z); R[1][0] = 2 * (x * y - z * w); R[2][0] = 2 * (x * z + y * w);
		R[0][1] = 2 * (x * y + z * w); R[1][1] = 1 - 2 * (x * x + z * z); R[2][1] = 2 * (y * z - x * w);
		R[0][2] = 2 * (x * z - y * w); R[1][2] = 2 * (y * z + x * w); R[2][2] = 1 - 2 * (x * x + y * y);
	}
	return r3d::matmul(T, r3d::matmul(R, S));
}

constexpr model parse_glb(const unsigned char * data, size_t len) {
	model out;
	if (len < 20 || !(data[0] == 'g' && data[1] == 'l' && data[2] == 'T' && data[3] == 'F')) { return out; }
	auto rd32 = [&](size_t o) { return read_u32le(data + o); };
	const uint32_t jlen = rd32(12);
	const std::string json_str(data + 20, data + 20 + jlen); // char copy (no reinterpret_cast)
	const size_t off = 20 + jlen;
	const unsigned char * bin = nullptr;
	if (off + 8 <= len) { bin = data + off + 8; }
	if (bin == nullptr) { return out; }

	const jval doc = json_parse(json_str);
	const jval * accessors = doc.get("accessors");
	const jval * bufferViews = doc.get("bufferViews");
	const jval * meshes = doc.get("meshes");
	const jval * nodes = doc.get("nodes");
	const jval * mats = doc.get("materials");
	if (accessors == nullptr || bufferViews == nullptr || meshes == nullptr || nodes == nullptr) { return out; }

	if (mats != nullptr) {
		for (size_t i = 0; i < mats->size(); ++i) {
			const jval & m = (*mats)[i];
			material mm;
			mm.name = m.get("name") ? m.get("name")->as_str() : ("mat" + cstr_int(i));
			if (const jval * pbr = m.get("pbrMetallicRoughness")) {
				if (const jval * bc = pbr->get("baseColorFactor")) {
					if (bc->size() >= 3) {
						mm.base = {(*bc)[0].as_num(1), (*bc)[1].as_num(1), (*bc)[2].as_num(1),
						           bc->size() > 3 ? (*bc)[3].as_num(1) : 1.0};
					}
				}
				if (const jval * bt = pbr->get("baseColorTexture")) {
					mm.base_tex = bt->get("index") ? bt->get("index")->as_int() : -1;
				}
			}
			out.materials.push_back(mm);
		}
	}

	// resolve each glTF texture -> its source image's encoded bytes (a GLB image
	// is a bufferView slice of the binary chunk). We copy the raw PNG/JPEG bytes;
	// decoding happens later at runtime (parse_glb stays constexpr).
	if (const jval * textures = doc.get("textures")) {
		const jval * images = doc.get("images");
		for (size_t i = 0; i < textures->size(); ++i) {
			std::vector<unsigned char> bytes;
			const jval * src = (*textures)[i].get("source");
			if (images != nullptr && src != nullptr) {
				const int im = src->as_int();
				if (im >= 0 && im < static_cast<int>(images->size())) {
					if (const jval * bvj = (*images)[static_cast<size_t>(im)].get("bufferView")) {
						const int bvi = bvj->as_int();
						if (bvi >= 0 && bvi < static_cast<int>(bufferViews->size())) {
							const jval & bv = (*bufferViews)[static_cast<size_t>(bvi)];
							const size_t bo = bv.get("byteOffset") ? static_cast<size_t>(bv.get("byteOffset")->as_num()) : 0;
							const size_t bl = bv.get("byteLength") ? static_cast<size_t>(bv.get("byteLength")->as_num()) : 0;
							bytes.assign(bin + bo, bin + bo + bl);
						}
					}
				}
			}
			out.textures.push_back(std::move(bytes));
		}
	}

	auto acc_view = [&](int acc, size_t & count, size_t & stride, int & ctype) -> const unsigned char * {
		count = 0; stride = 0; ctype = 5126;
		if (acc < 0 || acc >= static_cast<int>(accessors->size())) { return nullptr; }
		const jval & a = (*accessors)[static_cast<size_t>(acc)];
		count = static_cast<size_t>(a.get("count") ? a.get("count")->as_num() : 0);
		ctype = a.get("componentType") ? a.get("componentType")->as_int() : 5126;
		const int bvi = a.get("bufferView") ? a.get("bufferView")->as_int() : 0;
		if (bvi < 0 || bvi >= static_cast<int>(bufferViews->size())) { return nullptr; }
		const size_t aoff = a.get("byteOffset") ? static_cast<size_t>(a.get("byteOffset")->as_num()) : 0;
		const jval & bv = (*bufferViews)[static_cast<size_t>(bvi)];
		const size_t boff = bv.get("byteOffset") ? static_cast<size_t>(bv.get("byteOffset")->as_num()) : 0;
		stride = bv.get("byteStride") ? static_cast<size_t>(bv.get("byteStride")->as_num()) : 0;
		return bin + boff + aoff;
	};

	bool first = true;
	for (size_t ni = 0; ni < nodes->size(); ++ni) {
		const jval & node = (*nodes)[ni];
		if (node.get("mesh") == nullptr) { continue; }
		const r3d::mat4 xf = node_matrix(node);
		const int mi = node.get("mesh")->as_int();
		if (mi < 0 || mi >= static_cast<int>(meshes->size())) { continue; }
		const jval & mesh = (*meshes)[static_cast<size_t>(mi)];
		const jval * prims = mesh.get("primitives");
		if (prims == nullptr) { continue; }
		for (size_t pi = 0; pi < prims->size(); ++pi) {
			const jval & pr = (*prims)[pi];
			const jval * attr = pr.get("attributes");
			if (attr == nullptr || attr->get("POSITION") == nullptr || pr.get("indices") == nullptr) { continue; }
			primitive out_p;
			out_p.node_name = node.get("name") ? node.get("name")->as_str() : "";
			out_p.material = pr.get("material") ? pr.get("material")->as_int() : -1;

			// POSITION (VEC3 float), transformed by the node matrix
			size_t vcount, vstride; int vtype;
			const unsigned char * vp = acc_view(attr->get("POSITION")->as_int(), vcount, vstride, vtype);
			if (vp == nullptr) { continue; }
			if (vstride == 0) { vstride = 12; }
			out_p.verts.reserve(vcount);
			for (size_t i = 0; i < vcount; ++i) {
				const r3d::vec4 w = r3d::xform(xf, r3d::V3(read_f32le(vp + i * vstride),
				                                          read_f32le(vp + i * vstride + 4),
				                                          read_f32le(vp + i * vstride + 8)));
				// glTF is right-handed (+Z toward the viewer); this renderer is
				// left-handed (+Z into the screen). Negate Z (and flip the triangle
				// winding below) to convert - otherwise models face backwards.
				const r3d::vec3 v = r3d::V3(w[0], w[1], -w[2]);
				out_p.verts.push_back(v);
				if (first) { out.bmin = out.bmax = v; first = false; }
				for (int c = 0; c < 3; ++c) {
					out.bmin[c] = std::min(out.bmin[c], v[c]);
					out.bmax[c] = std::max(out.bmax[c], v[c]);
				}
			}
			// TEXCOORD_0 (VEC2) -> per-vertex UVs, parallel to verts. Float, or
			// normalized ubyte/ushort per the glTF accessor's componentType.
			if (const jval * tc = attr->get("TEXCOORD_0")) {
				size_t tcount, tstride; int ttype;
				const unsigned char * tp = acc_view(tc->as_int(), tcount, tstride, ttype);
				if (tp != nullptr) {
					const size_t st = tstride != 0 ? tstride
					                  : (ttype == 5126 ? 8u : (ttype == 5123 ? 4u : 2u));
					out_p.uvs.reserve(tcount);
					for (size_t i = 0; i < tcount; ++i) {
						const unsigned char * e = tp + i * st;
						double u = 0, v = 0;
						if (ttype == 5126) { u = read_f32le(e); v = read_f32le(e + 4); }
						else if (ttype == 5123) {
							u = (e[0] | (e[1] << 8)) / 65535.0;
							v = (e[2] | (e[3] << 8)) / 65535.0;
						} else { u = e[0] / 255.0; v = e[1] / 255.0; }
						out_p.uvs.push_back(r3d::V2(u, v));
					}
				}
			}

			// indices (ubyte/ushort/uint) -> triangles
			size_t icount, istride; int itype;
			const unsigned char * ip = acc_view(pr.get("indices")->as_int(), icount, istride, itype);
			if (ip == nullptr) { continue; }
			const size_t comp = itype == 5121 ? 1 : (itype == 5123 ? 2 : 4);
			std::vector<int> idx;
			idx.reserve(icount);
			for (size_t i = 0; i < icount; ++i) {
				uint32_t v = 0;
				for (size_t b = 0; b < comp; ++b) { v |= static_cast<uint32_t>(ip[i * comp + b]) << (8 * b); }
				idx.push_back(static_cast<int>(v));
			}
			for (size_t i = 0; i + 2 < idx.size(); i += 3) {
				// winding flipped (idx[i+2] before idx[i+1]) to match the Z negation
				// above, so faces stay outward after the right->left handed flip
				out_p.tris.push_back({idx[i], idx[i + 2], idx[i + 1]});
			}
			out.prims.push_back(std::move(out_p));
		}
	}
	out.ok = !out.prims.empty();
	return out;
}

} // namespace gltf

#ifndef CTBROWSER_BABYLON_RENDER_ONLY
// ============================================================
//  BABYLON.* bindings — factory-style ctjs natives over a shared world.
//  JS handles carry integer indices (__mesh/__scene/…) into the world's
//  C++ record vectors (the __node idiom). Transform/color objects are
//  plain data props the renderer reads back each frame.
// ============================================================

namespace detail {

using ctjs::object_t;
using ctjs::value;
using objptr = ctjs::rc<object_t>;

// --- small readers over JS handles
inline double num_prop(const objptr & o, const char * k, double dflt) {
	if (!o) { return dflt; }
	const value * v = o->find(k);
	return (v != nullptr && v->is_number()) ? v->as_number() : dflt;
}
inline objptr child_obj(const objptr & o, const char * k) {
	if (!o) { return {}; }
	const value * v = o->find(k);
	return (v != nullptr && v->is_object()) ? v->as_object() : objptr{};
}
inline r3d::vec3 read_vec3(const objptr & o, r3d::vec3 dflt) {
	if (!o) { return dflt; }
	return r3d::V3(num_prop(o, "x", dflt[0]), num_prop(o, "y", dflt[1]),
	               num_prop(o, "z", dflt[2]));
}
inline r3d::rgba read_color(const objptr & o, r3d::rgba dflt) {
	if (!o) { return dflt; }
	return {num_prop(o, "r", dflt.r), num_prop(o, "g", dflt.g), num_prop(o, "b", dflt.b),
	        num_prop(o, "a", dflt.a)};
}
inline double arg_num(const std::vector<value> & a, size_t i, double dflt) {
	if (i >= a.size()) { return dflt; }
	const double d = a[i].to_number();
	return std::isnan(d) ? dflt : d;
}
inline objptr arg_obj(const std::vector<value> & a, size_t i) {
	return (i < a.size() && a[i].is_object()) ? a[i].as_object() : objptr{};
}
inline objptr self_of(ctjs::context & cx) {
	return cx.current_this.is_object() ? cx.current_this.as_object() : objptr{};
}

// one registered Observable callback (id lets .remove(observer) find it)
struct observer {
	int id;
	value cb;
};

// --- world state
struct mesh_rec {
	r3d::geo geom;
	objptr handle;
	bool cull = true;
	bool disposed = false;
	bool enabled = true;
	std::vector<observer> before_render;   // mesh.onBeforeRenderObservable
	int scene_id = -1;                     // owning scene (for clone/createInstance)
	bool frozen_world = false;             // freezeWorldMatrix: use frozen_matrix, ignore live transforms
	r3d::mat4 frozen_matrix{};             // the world matrix captured at freeze time
	bool has_pivot = false;                // setPivotPoint: rotate/scale about `pivot`
	r3d::vec3 pivot{};
	std::shared_ptr<r3d::texture> tex{};   // glTF baseColor texture (shared across clones)
};
struct light_rec { objptr handle; int type; };
struct camera_rec { objptr handle; int type; bool attached = false; };
struct scene_rec {
	objptr handle;
	std::vector<int> mesh_ids, light_ids;
	int active_camera = -1;
	std::vector<std::pair<std::string, objptr>> materials; // for getMaterialById
	r3d::vec3 bmin{}, bmax{}; // model bounds (for createDefaultCamera)
	bool has_bounds = false;
	std::vector<observer> before_render;  // scene.onBeforeRenderObservable
	std::vector<observer> after_render;   // scene.registerAfterRender / onAfterRenderObservable
	std::vector<objptr> glow_layers;      // GlowLayer instances (additive bloom post-FX)
	std::vector<objptr> action_managers;  // scene ActionManagers (OnEveryFrameTrigger)
	bool active_meshes_frozen = false;    // freezeActiveMeshes (API-fidelity flag; no cull cache here)
	double delta_ms = 16.6;
};
struct world {
	ctbrowser::node * target = nullptr;
	r3d::renderer rdr;
	std::vector<mesh_rec> meshes;
	std::vector<light_rec> lights;
	std::vector<camera_rec> cameras;
	std::vector<scene_rec> scenes;
	value render_cb;
	value loop_wrapper;
	objptr engine_handle; // the BABYLON.Engine JS handle (scene.getEngine())
	bool loop_active = false;
	double prev_ms = 0, last_dt_ms = 16.6;
	int next_obs = 1;    // Observable observer-id counter
	int next_uid = 1;    // uniqueId / getUniqueId counter
	unsigned rng = 0x2545F4914F6CDD1Du & 0xffffffffu; // Scalar.RandomRange PRNG
	std::vector<objptr> guis;    // AdvancedDynamicTextures - rendered as a 2D overlay
	std::vector<objptr> sprites; // Sprites (starfield) - projected + drawn as quads
	// ArcRotate mouse-orbit state (one active camera at a time)
	bool cam_dragging = false;
	double cam_lastx = 0, cam_lasty = 0;
};
using worldptr = std::shared_ptr<world>;
inline int index_of(const objptr & handle, const char * key); // defined below

// a JS Observable bound to a rec's callback list: add(cb)->observer,
// remove(observer), clear(). is_mesh selects mesh vs scene sink by index.
inline value make_observable(const worldptr & W, int id, bool is_mesh) {
	auto o = objptr::make();
	const auto sink = [W, id, is_mesh]() -> std::vector<observer> * {
		if (is_mesh) {
			return (id >= 0 && id < static_cast<int>(W->meshes.size())) ? &W->meshes[static_cast<size_t>(id)].before_render : nullptr;
		}
		return (id >= 0 && id < static_cast<int>(W->scenes.size())) ? &W->scenes[static_cast<size_t>(id)].before_render : nullptr;
	};
	o->set("add", value::function([W, sink](ctjs::context &, const std::vector<value> & a) -> value {
		std::vector<observer> * v = sink();
		if (v == nullptr || a.empty() || !a[0].is_function()) { return value{}; }
		const int oid = W->next_obs++;
		v->push_back({oid, a[0]});
		auto obs = objptr::make();
		obs->set("__obs_id", value{static_cast<double>(oid)});
		return value{obs};
	}, "add"));
	o->set("remove", value::function([sink](ctjs::context &, const std::vector<value> & a) -> value {
		std::vector<observer> * v = sink();
		const int oid = (!a.empty() && a[0].is_object()) ? index_of(a[0].as_object(), "__obs_id") : -1;
		if (v != nullptr && oid >= 0) {
			for (size_t k = 0; k < v->size(); ++k) {
				if ((*v)[k].id == oid) { v->erase(v->begin() + static_cast<std::ptrdiff_t>(k)); break; }
			}
		}
		return value{true};
	}, "remove"));
	o->set("clear", value::function([sink](ctjs::context &, const std::vector<value> &) -> value {
		if (std::vector<observer> * v = sink()) { v->clear(); }
		return value{};
	}, "clear"));
	o->set("hasObservers", value::function([sink](ctjs::context &, const std::vector<value> &) -> value {
		std::vector<observer> * v = sink();
		return value{v != nullptr && !v->empty()};
	}, "hasObservers"));
	return value{o};
}

// an Observable that accepts add/remove but never fires (for events we do not
// model, e.g. engine.onResizeObservable) - keeps scripts from throwing
inline value make_dead_observable() {
	auto o = objptr::make();
	o->set("add", value::function([](ctjs::context &, const std::vector<value> &) -> value {
		auto obs = objptr::make();
		obs->set("__obs_id", value{0.0});
		return value{obs};
	}, "add"));
	o->set("remove", value::function([](ctjs::context &, const std::vector<value> &) { return value{true}; }, "remove"));
	o->set("clear", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "clear"));
	o->set("hasObservers", value::function([](ctjs::context &, const std::vector<value> &) { return value{false}; }, "hasObservers"));
	return value{o};
}

// fire scene.onBeforeRenderObservable then every live mesh's, once per frame.
// Copies the lists first: callbacks may add/remove observers or spawn/dispose
// meshes mid-iteration.
inline void fire_before_render(const worldptr & W, int scene_id, ctjs::context & cx) {
	if (scene_id < 0 || scene_id >= static_cast<int>(W->scenes.size())) { return; }
	const std::vector<observer> scene_cbs = W->scenes[static_cast<size_t>(scene_id)].before_render;
	for (const observer & ob : scene_cbs) { ctjs::call_value(cx, ob.cb, {}); }
	const std::vector<int> ids = W->scenes[static_cast<size_t>(scene_id)].mesh_ids;
	for (int mi : ids) {
		if (mi < 0 || mi >= static_cast<int>(W->meshes.size())) { continue; }
		if (W->meshes[static_cast<size_t>(mi)].disposed) { continue; }
		const std::vector<observer> mesh_cbs = W->meshes[static_cast<size_t>(mi)].before_render;
		for (const observer & ob : mesh_cbs) { ctjs::call_value(cx, ob.cb, {}); }
	}
}

// fire scene.registerAfterRender / onAfterRenderObservable callbacks (post-draw)
inline void fire_after_render(const worldptr & W, int scene_id, ctjs::context & cx) {
	if (scene_id < 0 || scene_id >= static_cast<int>(W->scenes.size())) { return; }
	const std::vector<observer> cbs = W->scenes[static_cast<size_t>(scene_id)].after_render;
	for (const observer & ob : cbs) { ctjs::call_value(cx, ob.cb, {}); }
}

// BABYLON.ActionManager trigger ids we actually fire (Babylon's enum values)
inline constexpr int TRIGGER_ON_EVERY_FRAME = 11;

// run each scene ActionManager's OnEveryFrameTrigger actions, once per frame
inline void fire_action_managers(const worldptr & W, int scene_id, ctjs::context & cx) {
	if (scene_id < 0 || scene_id >= static_cast<int>(W->scenes.size())) { return; }
	const std::vector<objptr> ams = W->scenes[static_cast<size_t>(scene_id)].action_managers;
	for (const objptr & am : ams) {
		if (!am) { continue; }
		const value * av = am->find("__actions");
		if (av == nullptr || !av->is_array()) { continue; }
		const std::vector<value> actions = *av->as_array(); // copy: an action may mutate the list
		for (const value & a : actions) {
			if (!a.is_object()) { continue; }
			const objptr act = a.as_object();
			if (index_of(act, "__trigger") != TRIGGER_ON_EVERY_FRAME) { continue; }
			if (const value * f = act->find("__func"); f != nullptr && f->is_function()) {
				ctjs::call_value(cx, *f, {});
			}
		}
	}
}
using worldptr = std::shared_ptr<world>;

inline int index_of(const objptr & handle, const char * key) {
	const double d = num_prop(handle, key, -1);
	return d < 0 ? -1 : static_cast<int>(d);
}

// --- Vector3 (data props x/y/z; methods read `this` via current_this)
inline value make_vector3(double x, double y, double z);

inline value make_vector3(double x, double y, double z) {
	auto o = objptr::make();
	o->set("x", value{x});
	o->set("y", value{y});
	o->set("z", value{z});
	o->set("set", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		objptr s = self_of(cx);
		if (s) { s->set("x", value{arg_num(a, 0, 0)}); s->set("y", value{arg_num(a, 1, 0)});
		         s->set("z", value{arg_num(a, 2, 0)}); }
		return cx.current_this;
	}, "set"));
	o->set("clone", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
		const r3d::vec3 v = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		return make_vector3(v[0], v[1], v[2]);
	}, "clone"));
	o->set("add", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const r3d::vec3 s = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		const r3d::vec3 t = read_vec3(arg_obj(a, 0), r3d::V3(0, 0, 0));
		return make_vector3(s[0] + t[0], s[1] + t[1], s[2] + t[2]);
	}, "add"));
	o->set("subtract", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const r3d::vec3 s = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		const r3d::vec3 t = read_vec3(arg_obj(a, 0), r3d::V3(0, 0, 0));
		return make_vector3(s[0] - t[0], s[1] - t[1], s[2] - t[2]);
	}, "subtract"));
	o->set("scale", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const r3d::vec3 s = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		const double k = arg_num(a, 0, 1);
		return make_vector3(s[0] * k, s[1] * k, s[2] * k);
	}, "scale"));
	o->set("length", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
		const r3d::vec3 s = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		return value{std::sqrt(s[0]*s[0] + s[1]*s[1] + s[2]*s[2])};
	}, "length"));
	o->set("normalize", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
		const r3d::vec3 n = r3d::norm3(read_vec3(self_of(cx), r3d::V3(0, 0, 0)));
		objptr s = self_of(cx);
		if (s) { s->set("x", value{n[0]}); s->set("y", value{n[1]}); s->set("z", value{n[2]}); }
		return cx.current_this;
	}, "normalize"));
	return value{o};
}

inline value make_color4(double r, double g, double b, double a);

inline value make_color3(double r, double g, double b) {
	auto o = objptr::make();
	o->set("r", value{r});
	o->set("g", value{g});
	o->set("b", value{b});
	o->set("toColor4", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const objptr s = self_of(cx);
		return make_color4(num_prop(s, "r", 0), num_prop(s, "g", 0), num_prop(s, "b", 0), arg_num(a, 0, 1));
	}, "toColor4"));
	o->set("clone", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
		const objptr s = self_of(cx);
		return make_color3(num_prop(s, "r", 0), num_prop(s, "g", 0), num_prop(s, "b", 0));
	}, "clone"));
	o->set("scale", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const objptr s = self_of(cx);
		const double k = arg_num(a, 0, 1);
		return make_color3(num_prop(s, "r", 0) * k, num_prop(s, "g", 0) * k, num_prop(s, "b", 0) * k);
	}, "scale"));
	return value{o};
}
inline value make_color4(double r, double g, double b, double a) {
	auto o = objptr::make();
	o->set("r", value{r});
	o->set("g", value{g});
	o->set("b", value{b});
	o->set("a", value{a});
	o->set("clone", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
		const objptr s = self_of(cx);
		return make_color4(num_prop(s, "r", 0), num_prop(s, "g", 0), num_prop(s, "b", 0), num_prop(s, "a", 1));
	}, "clone"));
	o->set("toColor3", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
		const objptr s = self_of(cx);
		return make_color3(num_prop(s, "r", 0), num_prop(s, "g", 0), num_prop(s, "b", 0));
	}, "toColor3"));
	return value{o};
}

// attach statics to a function value (rides on function_t::props)
inline void set_static(value & fn, const char * name, value v) {
	if (!fn.is_function()) { return; }
	if (!fn.as_function()->props) { fn.as_function()->props = objptr::make(); }
	fn.as_function()->props->set(name, std::move(v));
}

// --- the 2D overlay drawn OVER the 3D pass (these were no-op stubs).

// a font8x8 string blitted into the pixel buffer (top-left origin)
inline void overlay_text(uint32_t * px, int w, int h, int x0, int y0, std::string_view s,
                         int scale, uint32_t argb) {
	int pen = x0;
	for (std::size_t i = 0; i < s.size();) { // decode UTF-8 -> code points
		const char32_t ch = ctbrowser::utf8_next(s, i);
		for (int row = 0; row < 8; ++row) {
			for (int col = 0; col < 8; ++col) {
				if (!ctbrowser::detail::glyph_pixel(ch, row, col)) { continue; }
				for (int sy = 0; sy < scale; ++sy) {
					for (int sx = 0; sx < scale; ++sx) {
						const int x = pen + col * scale + sx, y = y0 + row * scale + sy;
						if (x >= 0 && x < w && y >= 0 && y < h) { px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = argb; }
					}
				}
			}
		}
		pen += 8 * scale;
	}
}

// BABYLON.GUI TextBlocks (the score/level/lives HUD): positioned by alignment +
// left/top offsets, coloured from the CSS colour string, drawn in font8x8
inline void render_guis(const worldptr & W, uint32_t * px, int w, int h) {
	for (const objptr & gui : W->guis) {
		const value * cs = gui->find("controls");
		if (cs == nullptr || !cs->is_array()) { continue; }
		for (const value & cv : *cs->as_array()) {
			if (!cv.is_object()) { continue; }
			const objptr c = cv.as_object();
			const value * t = c->find("text");
			if (t == nullptr) { continue; } // TextBlocks carry .text
			if (const value * vis = c->find("isVisible"); vis != nullptr && !vis->truthy()) { continue; }
			const std::string text = t->to_string();
			if (text.empty()) { continue; }
			const double fs = num_prop(c, "fontSize", 18);
			const int scale = fs >= 8 ? static_cast<int>(fs / 8) : 1;
			uint32_t col = 0xFFFFFFFFu;
			if (const value * cc = c->find("color")) {
				col = ctbrowser::detail::css_to_argb(cc->to_string(), 0xFFFFFFFFu);
			}
			const int halign = static_cast<int>(num_prop(c, "textHorizontalAlignment", 2));
			const int valign = static_cast<int>(num_prop(c, "textVerticalAlignment", 2));
			const int tw = static_cast<int>(text.size()) * 8 * scale, th = 8 * scale;
			int x = (halign == 0) ? 0 : (halign == 1) ? (w - tw) : (w - tw) / 2;
			int y = (valign == 0) ? 0 : (valign == 1) ? (h - th) : (h - th) / 2;
			x += static_cast<int>(num_prop(c, "left", 0));
			y += static_cast<int>(num_prop(c, "top", 0));
			overlay_text(px, w, h, x, y, text, scale, col);
		}
	}
}

// Sprites (the starfield): project each 3D position through the camera and draw
// a screen-space quad in the sprite's colour
inline void render_sprites(const worldptr & W, uint32_t * px, int w, int h, const r3d::view & vw) {
	if (W->sprites.empty()) { return; }
	const r3d::mat4 vp = r3d::matmul(vw.vp_proj, vw.vp_view);
	const double Wd = w, Hd = h;
	for (const objptr & sp : W->sprites) {
		if (const value * vis = sp->find("isVisible"); vis != nullptr && !vis->truthy()) { continue; }
		const r3d::vec3 pos = read_vec3(child_obj(sp, "position"), r3d::V3(0, 0, 0));
		const r3d::vec4 clip = r3d::xform(vp, pos);
		if (clip[3] <= 1e-6) { continue; } // behind the camera
		const double iw = 1.0 / clip[3];
		const int sx = static_cast<int>((clip[0] * iw * 0.5 + 0.5) * Wd);
		const int sy = static_cast<int>((1.0 - (clip[1] * iw * 0.5 + 0.5)) * Hd);
		if (sx < 0 || sx >= w || sy < 0 || sy >= h) { continue; }
		const r3d::rgba c = read_color(child_obj(sp, "color"), r3d::rgba{1, 1, 1, 1});
		// perspective-scaled half-extent (fov 0.8 -> 2*tan(0.4) ~ 0.8455)
		int half = static_cast<int>(num_prop(sp, "size", 1.0) * 0.5 * Hd * iw / 0.8455);
		half = half < 0 ? 0 : half > 32 ? 32 : half;
		// draw a soft, additively-blended glowing dot (a bright solid core plus a
		// quadratic-falloff halo) - this is what the game's star_glow.png sprite
		// gives on real WebGL; here it makes the starfield actually glow.
		const int rad = std::max(half + 3, 4);
		const double core = static_cast<double>(half);
		const double span = static_cast<double>(rad) - core;
		const auto add = [](uint32_t base, double v) {
			const uint32_t s = base + static_cast<uint32_t>(v < 0 ? 0 : v);
			return s > 255 ? 255u : s;
		};
		for (int dy = -rad; dy <= rad; ++dy) {
			for (int dx = -rad; dx <= rad; ++dx) {
				const int x = sx + dx, y = sy + dy;
				if (x < 0 || x >= w || y < 0 || y >= h) { continue; }
				const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
				double f;
				if (dist <= core) {
					f = 1.0;
				} else {
					const double t = (dist - core) / span; // 0 at core edge, 1 at halo edge
					if (t >= 1.0) { continue; }
					f = (1.0 - t) * (1.0 - t);
				}
				const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
				const uint32_t p = px[idx];
				const uint32_t nr = add((p >> 16) & 0xff, c.r * 255.0 * f);
				const uint32_t ng = add((p >> 8) & 0xff, c.g * 255.0 * f);
				const uint32_t nb = add(p & 0xff, c.b * 255.0 * f);
				px[idx] = 0xFF000000u | (nr << 16) | (ng << 8) | nb;
			}
		}
	}
}

// --- the core render: read the scene's meshes/lights/camera back from
//     their live JS handles and rasterize into the target canvas pixels
// GlowLayer post-process: additive bloom. Extract pixels brighter than a
// luminance threshold, blur them into a soft halo (separable box blur, two
// passes ~= gaussian), and add the halo back scaled by `intensity`. Emissive
// geometry (bright lasers, HUD text, the neon UI) gains a glow, which is what
// BABYLON.GlowLayer does on real WebGL - here on the software framebuffer.
// `mask` (optional, w*h) restricts which pixels may SEED the bloom - used by the
// GlowLayer include/exclude lists: only pixels covered by a glowing mesh glow.
// nullptr = the whole frame seeds (bloom every bright pixel).
inline void apply_glow(uint32_t * px, int w, int h, double intensity,
                       const std::vector<uint8_t> * mask = nullptr) {
	if (intensity <= 0 || w <= 0 || h <= 0) { return; }
	const int n = w * h;
	const auto at = [w](int x, int y, int c) { return static_cast<size_t>((y * w + x) * 3 + c); };
	std::vector<float> br(static_cast<size_t>(n) * 3, 0.0f);
	const float thr = 0.55f;
	for (int i = 0; i < n; ++i) {
		if (mask != nullptr && (*mask)[static_cast<size_t>(i)] == 0) { continue; }
		const uint32_t p = px[static_cast<size_t>(i)];
		const float r = static_cast<float>((p >> 16) & 0xff) / 255.0f;
		const float g = static_cast<float>((p >> 8) & 0xff) / 255.0f;
		const float b = static_cast<float>(p & 0xff) / 255.0f;
		const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
		if (lum > thr) {
			const float k = (lum - thr) / (1.0f - thr);
			br[static_cast<size_t>(i) * 3 + 0] = r * k;
			br[static_cast<size_t>(i) * 3 + 1] = g * k;
			br[static_cast<size_t>(i) * 3 + 2] = b * k;
		}
	}
	const int rad = 3;
	std::vector<float> tmp(br.size(), 0.0f);
	for (int pass = 0; pass < 2; ++pass) {
		for (int y = 0; y < h; ++y) { // horizontal
			for (int x = 0; x < w; ++x) {
				for (int c = 0; c < 3; ++c) {
					float s = 0; int cnt = 0;
					for (int dx = -rad; dx <= rad; ++dx) {
						const int xx = x + dx;
						if (xx < 0 || xx >= w) { continue; }
						s += br[at(xx, y, c)]; ++cnt;
					}
					tmp[at(x, y, c)] = s / static_cast<float>(cnt);
				}
			}
		}
		for (int x = 0; x < w; ++x) { // vertical
			for (int y = 0; y < h; ++y) {
				for (int c = 0; c < 3; ++c) {
					float s = 0; int cnt = 0;
					for (int dy = -rad; dy <= rad; ++dy) {
						const int yy = y + dy;
						if (yy < 0 || yy >= h) { continue; }
						s += tmp[at(x, yy, c)]; ++cnt;
					}
					br[at(x, y, c)] = s / static_cast<float>(cnt);
				}
			}
		}
	}
	const float gain = static_cast<float>(intensity);
	const auto clamp01 = [](float v) { return v < 0 ? 0.0f : v > 1 ? 1.0f : v; };
	for (int i = 0; i < n; ++i) {
		const uint32_t p = px[static_cast<size_t>(i)];
		const float r = clamp01(static_cast<float>((p >> 16) & 0xff) / 255.0f + br[static_cast<size_t>(i) * 3 + 0] * gain);
		const float g = clamp01(static_cast<float>((p >> 8) & 0xff) / 255.0f + br[static_cast<size_t>(i) * 3 + 1] * gain);
		const float b = clamp01(static_cast<float>(p & 0xff) / 255.0f + br[static_cast<size_t>(i) * 3 + 2] * gain);
		px[static_cast<size_t>(i)] = (p & 0xff000000u) |
		    (static_cast<uint32_t>(r * 255.0f) << 16) |
		    (static_cast<uint32_t>(g * 255.0f) << 8) |
		    static_cast<uint32_t>(b * 255.0f);
	}
}

// the mesh's world matrix from its live transforms (position * pivot * rotation *
// scaling * pivot^-1), or the captured matrix if freezeWorldMatrix() was called.
inline r3d::mat4 mesh_world_matrix(const worldptr & W, int mi, bool ignore_freeze = false) {
	mesh_rec & M = W->meshes[static_cast<size_t>(mi)];
	if (M.frozen_world && !ignore_freeze) { return M.frozen_matrix; }
	const r3d::vec3 p = read_vec3(child_obj(M.handle, "position"), r3d::V3(0, 0, 0));
	const r3d::vec3 rot = read_vec3(child_obj(M.handle, "rotation"), r3d::V3(0, 0, 0));
	const r3d::vec3 s = read_vec3(child_obj(M.handle, "scaling"), r3d::V3(1, 1, 1));
	r3d::mat4 rs = r3d::matmul(r3d::rotationYPR(rot[0], rot[1], rot[2]),
	                          r3d::scaling(s[0], s[1], s[2]));
	if (M.has_pivot) { // rotate/scale about the pivot: T(pivot) * R*S * T(-pivot)
		rs = r3d::matmul(r3d::translation(M.pivot[0], M.pivot[1], M.pivot[2]),
		     r3d::matmul(rs, r3d::translation(-M.pivot[0], -M.pivot[1], -M.pivot[2])));
	}
	return r3d::matmul(r3d::translation(p[0], p[1], p[2]), rs);
}

// build the renderer draw-item for a scene mesh (world matrix + material colour)
inline r3d::draw_item build_draw_item(const worldptr & W, int mi) {
	mesh_rec & M = W->meshes[static_cast<size_t>(mi)];
	r3d::draw_item it;
	it.g = &M.geom;
	it.world = mesh_world_matrix(W, mi);
	const objptr mat = child_obj(M.handle, "material");
	r3d::rgba mc{0.85, 0.85, 0.85, 1};
	if (mat) {
		if (child_obj(mat, "baseColor")) { mc = read_color(child_obj(mat, "baseColor"), mc); }
		else if (child_obj(mat, "albedoColor")) { mc = read_color(child_obj(mat, "albedoColor"), mc); }
		else { mc = read_color(child_obj(mat, "diffuseColor"), mc); }
	}
	// mesh.visibility (0..1) dims the color so a fading particle darkens out
	const double vis = num_prop(M.handle, "visibility", 1.0);
	if (vis < 1.0) { mc.r *= vis; mc.g *= vis; mc.b *= vis; }
	it.diffuse = mc;
	// glTF baseColor texture (sampled per pixel, tinted by `diffuse`)
	it.tex = (M.tex && M.tex->valid()) ? M.tex.get() : nullptr;
	it.cull = M.cull;
	return it;
}

// a BABYLON.Matrix wrapper: `.m` is the 16-element column-major store of the
// row-vector matrix (so m[12..14] is the translation, per Babylon), plus
// getTranslation(). Our r3d::mat4 is a[row][col] for column-vector M*p, hence the
// transpose on flatten.
inline value make_matrix(const r3d::mat4 & m) {
	auto o = objptr::make();
	std::vector<value> arr;
	for (int c = 0; c < 4; ++c) {
		for (int r = 0; r < 4; ++r) { arr.push_back(value{m[c][r]}); }
	}
	o->set("m", value::array(std::move(arr)));
	o->set("getTranslation", value::function([m](ctjs::context &, const std::vector<value> &) -> value {
		return make_vector3(m[3][0], m[3][1], m[3][2]);
	}, "getTranslation"));
	return value{o};
}

// read an array of mesh __mesh ids stored on a JS object (glow include/exclude)
inline void read_id_list(const objptr & o, const char * key, std::vector<int> & out) {
	const value * v = o->find(key);
	if (v == nullptr || !v->is_array()) { return; }
	for (const value & e : *v->as_array()) { out.push_back(static_cast<int>(e.to_number())); }
}

inline void do_render(const worldptr & W, int scene_id) {
	if (!W || W->target == nullptr || scene_id < 0 ||
	    scene_id >= static_cast<int>(W->scenes.size())) { return; }
	ctbrowser::node * n = W->target;
	const int w = n->canvas_w, h = n->canvas_h;
	if (w <= 0 || h <= 0 || n->pixels.empty()) { return; }
	scene_rec & sc = W->scenes[static_cast<size_t>(scene_id)];

	r3d::view vw;
	vw.clear = read_color(child_obj(sc.handle, "clearColor"), r3d::rgba{0.2, 0.2, 0.3, 1.0});

	r3d::vec3 eye = r3d::V3(0, 0, -10), target = r3d::V3(0, 0, 0);
	const r3d::vec3 up = r3d::V3(0, 1, 0);
	if (sc.active_camera >= 0 && sc.active_camera < static_cast<int>(W->cameras.size())) {
		camera_rec & cam = W->cameras[static_cast<size_t>(sc.active_camera)];
		if (cam.type == 0) { // ArcRotate: eye from spherical coords about target
			const double alpha = num_prop(cam.handle, "alpha", 0);
			const double beta = num_prop(cam.handle, "beta", 1);
			const double radius = num_prop(cam.handle, "radius", 10);
			target = read_vec3(child_obj(cam.handle, "target"), r3d::V3(0, 0, 0));
			eye = r3d::V3(target[0] + radius * r3d::fcos(alpha) * r3d::fsin(beta),
			              target[1] + radius * r3d::fcos(beta),
			              target[2] + radius * r3d::fsin(alpha) * r3d::fsin(beta));
		} else { // Free: explicit position, look toward its target/forward
			eye = read_vec3(child_obj(cam.handle, "position"), r3d::V3(0, 0, -10));
			target = read_vec3(child_obj(cam.handle, "target"), r3d::V3(0, 0, 0));
		}
	}
	vw.vp_view = r3d::lookAtLH(eye, target, up);
	vw.vp_proj = r3d::perspectiveFovLH(0.8, static_cast<double>(w) / h, 0.1, 1000.0);

	std::vector<r3d::light> lights;
	for (int li : sc.light_ids) {
		light_rec & L = W->lights[static_cast<size_t>(li)];
		r3d::light rl;
		rl.type = L.type;
		rl.direction = read_vec3(child_obj(L.handle, "direction"), r3d::V3(0, 1, 0));
		rl.intensity = num_prop(L.handle, "intensity", 1.0);
		rl.diffuse = read_color(child_obj(L.handle, "diffuse"), r3d::rgba{1, 1, 1, 1});
		lights.push_back(rl);
	}
	if (lights.empty()) { lights.push_back(r3d::light{0, r3d::V3(0, 1, 0), 1.0, {1, 1, 1, 1}}); }

	std::vector<r3d::draw_item> items;
	for (int mi : sc.mesh_ids) {
		mesh_rec & M = W->meshes[static_cast<size_t>(mi)];
		if (M.disposed || !M.enabled) { continue; }
		// honor mesh.isVisible (bool) and mesh.visibility (0..1 opacity): the game
		// fades explosion particles to visibility 0 to hide them (never disposed)
		if (const value * iv = M.handle->find("isVisible"); iv != nullptr && !iv->truthy()) { continue; }
		if (num_prop(M.handle, "visibility", 1.0) <= 0.02) { continue; }
		items.push_back(build_draw_item(W, mi));
	}
	W->rdr.render(n->pixels.data(), w, h, vw, items, lights);
	render_sprites(W, n->pixels.data(), w, h, vw);
	render_guis(W, n->pixels.data(), w, h);
	// GlowLayer bloom post-process (skips disposed layers; live intensity). With
	// an include/exclude list, a coverage mask of the glowing meshes restricts
	// which pixels seed the bloom.
	for (const objptr & gl : sc.glow_layers) {
		if (!gl || index_of(gl, "__disposed") == 1) { continue; }
		const double gi = num_prop(gl, "intensity", 1.0);
		std::vector<int> inc, exc;
		read_id_list(gl, "includedOnlyMeshes", inc);
		read_id_list(gl, "excludedMeshes", exc);
		if (inc.empty() && exc.empty()) {
			apply_glow(n->pixels.data(), w, h, gi);           // whole frame glows
			continue;
		}
		// render only the glowing meshes into a scratch buffer -> coverage mask
		std::vector<r3d::draw_item> gitems;
		for (int mi : sc.mesh_ids) {
			mesh_rec & M = W->meshes[static_cast<size_t>(mi)];
			if (M.disposed || !M.enabled) { continue; }
			if (!inc.empty() && std::find(inc.begin(), inc.end(), mi) == inc.end()) { continue; }
			if (std::find(exc.begin(), exc.end(), mi) != exc.end()) { continue; }
			gitems.push_back(build_draw_item(W, mi));
		}
		std::vector<uint32_t> scratch(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
		r3d::view gv = vw;
		gv.clear = r3d::rgba{0, 0, 0, 0};                     // transparent: alpha marks coverage
		W->rdr.render(scratch.data(), w, h, gv, gitems, lights);
		std::vector<uint8_t> mask(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
		for (size_t i = 0; i < mask.size(); ++i) { mask[i] = (scratch[i] >> 24) != 0 ? 1 : 0; }
		apply_glow(n->pixels.data(), w, h, gi, &mask);
	}
}

// --- register a mesh/light with its scene (by the scene handle arg)
inline void register_with_scene(const worldptr & W, const objptr & scene, int id, bool is_mesh) {
	const int si = index_of(scene, "__scene");
	if (si < 0 || si >= static_cast<int>(W->scenes.size())) { return; }
	(is_mesh ? W->scenes[static_cast<size_t>(si)].mesh_ids
	         : W->scenes[static_cast<size_t>(si)].light_ids).push_back(id);
	if (is_mesh && id >= 0 && id < static_cast<int>(W->meshes.size())) {
		W->meshes[static_cast<size_t>(id)].scene_id = si;
	}
}

// world-space axis-aligned bounds of a mesh (geometry bounds x scaling +
// position), for moveWithCollisions
inline void mesh_aabb(const worldptr & W, int id, r3d::vec3 & lo, r3d::vec3 & hi) {
	const mesh_rec & m = W->meshes[static_cast<size_t>(id)];
	const r3d::vec3 p = read_vec3(child_obj(m.handle, "position"), r3d::V3(0, 0, 0));
	const r3d::vec3 s = read_vec3(child_obj(m.handle, "scaling"), r3d::V3(1, 1, 1));
	bool first = true;
	r3d::vec3 gmin = r3d::V3(0, 0, 0), gmax = r3d::V3(0, 0, 0);
	for (const r3d::vec3 & v : m.geom.verts) {
		for (int k = 0; k < 3; ++k) {
			if (first) { gmin[k] = gmax[k] = v[k]; }
			else { gmin[k] = std::min(gmin[k], v[k]); gmax[k] = std::max(gmax[k], v[k]); }
		}
		first = false;
	}
	for (int k = 0; k < 3; ++k) {
		lo[k] = p[k] + gmin[k] * s[k];
		hi[k] = p[k] + gmax[k] * s[k];
	}
}

inline void decorate_mesh(const worldptr & W, const objptr & h, int id);

inline value make_mesh(const worldptr & W, r3d::geo g, std::string name, bool cull,
                       const objptr & scene) {
	const int id = static_cast<int>(W->meshes.size());
	auto h = objptr::make();
	h->set("name", value{std::move(name)});
	h->set("__mesh", value{static_cast<double>(id)});
	h->set("position", make_vector3(0, 0, 0));
	h->set("rotation", make_vector3(0, 0, 0));
	h->set("scaling", make_vector3(1, 1, 1));
	h->set("material", value{});
	W->meshes.push_back(mesh_rec{std::move(g), h, cull, false, true, {}, -1});
	register_with_scene(W, scene, id, true);
	decorate_mesh(W, h, id);
	return value{h};
}

inline value make_material(std::string name) {
	auto m = objptr::make();
	m->set("name", value{std::move(name)});
	m->set("diffuseColor", make_color3(0.85, 0.85, 0.85));
	m->set("specularColor", make_color3(1, 1, 1));
	m->set("emissiveColor", make_color3(0, 0, 0));
	m->set("ambientColor", make_color3(0, 0, 0));
	m->set("alpha", value{1.0});
	m->set("wireframe", value{false});
	return value{m};
}

// the full runtime Mesh surface the game drives: transforms, metadata,
// collision, clone/createInstance, moveWithCollisions, translate/rotate, the
// per-mesh onBeforeRenderObservable, dispose/onDispose. Called from every mesh
// factory (MeshBuilder, glTF, clone) after the rec + handle exist.
inline void decorate_mesh(const worldptr & W, const objptr & h, int id) {
	const std::size_t ix = static_cast<std::size_t>(id);
	if (h->find("position") == nullptr) { h->set("position", make_vector3(0, 0, 0)); }
	if (h->find("rotation") == nullptr) { h->set("rotation", make_vector3(0, 0, 0)); }
	if (h->find("scaling") == nullptr) { h->set("scaling", make_vector3(1, 1, 1)); }
	h->set("metadata", value{objptr::make()});
	if (h->find("uniqueId") == nullptr) { h->set("uniqueId", value{static_cast<double>(W->next_uid++)}); }
	h->set("isVisible", value{true});
	h->set("visibility", value{1.0});
	h->set("isPickable", value{true});
	h->set("checkCollisions", value{false});
	h->set("collisionGroup", value{-1.0});
	h->set("collisionMask", value{-1.0});
	h->set("collisionResponse", value{true});
	h->set("collisionRetryCount", value{3.0});
	h->set("onDispose", value{});
	h->set("instancedBuffers", value{objptr::make()});
	{
		auto col = objptr::make();
		col->set("collidedMesh", value{});
		h->set("collider", value{col});
	}
	h->set("onBeforeRenderObservable", make_observable(W, id, true));
	h->set("registerInstancedBuffer", value::function(
	    [](ctjs::context &, const std::vector<value> &) { return value{}; }, "registerInstancedBuffer"));

	h->set("getScene", value::function([W, ix](ctjs::context &, const std::vector<value> &) -> value {
		const int si = ix < W->meshes.size() ? W->meshes[ix].scene_id : -1;
		return (si >= 0 && si < static_cast<int>(W->scenes.size())) ? value{W->scenes[static_cast<std::size_t>(si)].handle} : value{};
	}, "getScene"));
	h->set("setEnabled", value::function([W, ix](ctjs::context &, const std::vector<value> & a) -> value {
		if (ix < W->meshes.size()) { W->meshes[ix].enabled = a.empty() || a[0].truthy(); }
		return value{};
	}, "setEnabled"));
	h->set("isEnabled", value::function([W, ix](ctjs::context &, const std::vector<value> &) -> value {
		return value{ix < W->meshes.size() && W->meshes[ix].enabled && !W->meshes[ix].disposed};
	}, "isEnabled"));
	h->set("dispose", value::function([W, ix](ctjs::context & cx, const std::vector<value> &) -> value {
		if (ix >= W->meshes.size()) { return value{}; }
		mesh_rec & m = W->meshes[ix];
		if (m.disposed) { return value{}; }
		m.disposed = true;
		m.before_render.clear();
		// free the heavy retained data now - a disposed mesh is never rendered
		// again, so its geometry and texture are dead weight (models carry
		// hundreds of verts + a texture). The slot itself stays (stable __mesh
		// indices), and the JS handle stays live for the onDispose callback below.
		m.geom = r3d::geo{};
		m.tex.reset();
		// keep the handle alive: onDispose may spawn meshes (Explosion) and
		// reallocate W->meshes, dangling `m`
		const objptr self = m.handle;
		const int sid = m.scene_id;
		if (sid >= 0 && sid < static_cast<int>(W->scenes.size())) {
			auto & ids = W->scenes[static_cast<std::size_t>(sid)].mesh_ids;
			ids.erase(std::remove(ids.begin(), ids.end(), static_cast<int>(ix)), ids.end());
		}
		// BabylonJS notifies onDispose WITH the mesh (its callbacks take it)
		const value * od = self->find("onDispose");
		if (od != nullptr && od->is_function()) {
			cx.pending_this = value{self};
			ctjs::call_value(cx, *od, {value{self}});
		}
		return value{};
	}, "dispose"));

	// clone(name)/createInstance(name): new mesh, same geometry + a COPY of the
	// current transform, in the same scene
	const auto cloner = [W, ix](ctjs::context &, const std::vector<value> & a) -> value {
		if (ix >= W->meshes.size()) { return value{}; }
		// snapshot everything from the source BEFORE push_back may reallocate
		mesh_rec & src = W->meshes[ix];
		r3d::geo geo = src.geom;
		const bool cull = src.cull;
		const int ssid = src.scene_id;
		std::shared_ptr<r3d::texture> stex = src.tex; // clones share the texture
		const r3d::vec3 p = read_vec3(child_obj(src.handle, "position"), r3d::V3(0, 0, 0));
		const r3d::vec3 r = read_vec3(child_obj(src.handle, "rotation"), r3d::V3(0, 0, 0));
		const r3d::vec3 s = read_vec3(child_obj(src.handle, "scaling"), r3d::V3(1, 1, 1));
		const value * mat = src.handle->find("material");
		const value matv = mat != nullptr ? *mat : value{};

		const int nid = static_cast<int>(W->meshes.size());
		auto nh = objptr::make();
		const std::string nm = a.empty() ? std::string{} : a[0].to_string();
		nh->set("name", value{nm});
		nh->set("id", value{nm});
		nh->set("__mesh", value{static_cast<double>(nid)});
		nh->set("position", make_vector3(p[0], p[1], p[2]));
		nh->set("rotation", make_vector3(r[0], r[1], r[2]));
		nh->set("scaling", make_vector3(s[0], s[1], s[2]));
		nh->set("material", matv);
		mesh_rec rec;
		rec.geom = std::move(geo);
		rec.handle = nh;
		rec.cull = cull;
		rec.scene_id = ssid;
		rec.tex = std::move(stex);
		W->meshes.push_back(std::move(rec));
		if (ssid >= 0 && ssid < static_cast<int>(W->scenes.size())) {
			W->scenes[static_cast<std::size_t>(ssid)].mesh_ids.push_back(nid);
		}
		decorate_mesh(W, nh, nid);
		return value{nh};
	};
	h->set("clone", value::function(cloner, "clone"));
	h->set("createInstance", value::function(cloner, "createInstance"));

	// moveWithCollisions(v): move by v, then set collider.collidedMesh to the
	// first overlapping mesh whose group this mesh's mask selects
	h->set("moveWithCollisions", value::function([W, ix](ctjs::context &, const std::vector<value> & a) -> value {
		if (ix >= W->meshes.size()) { return value{}; }
		const objptr self = W->meshes[ix].handle;
		const objptr pos = child_obj(self, "position");
		const r3d::vec3 v = read_vec3(arg_obj(a, 0), r3d::V3(0, 0, 0));
		if (pos) {
			pos->set("x", value{num_prop(pos, "x", 0) + v[0]});
			pos->set("y", value{num_prop(pos, "y", 0) + v[1]});
			pos->set("z", value{num_prop(pos, "z", 0) + v[2]});
		}
		r3d::vec3 lo, hi;
		mesh_aabb(W, static_cast<int>(ix), lo, hi);
		const int mask = static_cast<int>(num_prop(self, "collisionMask", -1));
		objptr hit;
		for (std::size_t mj = 0; mj < W->meshes.size(); ++mj) {
			if (mj == ix) { continue; }
			mesh_rec & o = W->meshes[mj];
			if (o.disposed || !o.enabled) { continue; }
			const int grp = static_cast<int>(num_prop(o.handle, "collisionGroup", -1));
			if ((mask & grp) == 0) { continue; }
			r3d::vec3 olo, ohi;
			mesh_aabb(W, static_cast<int>(mj), olo, ohi);
			const bool overlap = lo[0] <= ohi[0] && hi[0] >= olo[0] && lo[1] <= ohi[1] &&
			                     hi[1] >= olo[1] && lo[2] <= ohi[2] && hi[2] >= olo[2];
			if (overlap) { hit = o.handle; break; }
		}
		if (const objptr col = child_obj(self, "collider")) {
			col->set("collidedMesh", hit ? value{hit} : value{});
		}
		return value{};
	}, "moveWithCollisions"));

	h->set("calcMovePOV", value::function([](ctjs::context &, const std::vector<value> & a) -> value {
		return make_vector3(arg_num(a, 0, 0), arg_num(a, 1, 0), arg_num(a, 2, 0));
	}, "calcMovePOV"));
	h->set("translate", value::function([W, ix](ctjs::context &, const std::vector<value> & a) -> value {
		if (ix >= W->meshes.size()) { return value{}; }
		const objptr pos = child_obj(W->meshes[ix].handle, "position");
		const r3d::vec3 ax = read_vec3(arg_obj(a, 0), r3d::V3(0, 0, 0));
		const double d = arg_num(a, 1, 1);
		if (pos) {
			pos->set("x", value{num_prop(pos, "x", 0) + ax[0] * d});
			pos->set("y", value{num_prop(pos, "y", 0) + ax[1] * d});
			pos->set("z", value{num_prop(pos, "z", 0) + ax[2] * d});
		}
		return value{};
	}, "translate"));
	h->set("rotate", value::function([W, ix](ctjs::context &, const std::vector<value> & a) -> value {
		if (ix >= W->meshes.size()) { return value{}; }
		const objptr rot = child_obj(W->meshes[ix].handle, "rotation");
		const r3d::vec3 ax = read_vec3(arg_obj(a, 0), r3d::V3(0, 1, 0));
		const double amt = arg_num(a, 1, 0);
		if (rot) {
			rot->set("x", value{num_prop(rot, "x", 0) + ax[0] * amt});
			rot->set("y", value{num_prop(rot, "y", 0) + ax[1] * amt});
			rot->set("z", value{num_prop(rot, "z", 0) + ax[2] * amt});
		}
		return value{};
	}, "rotate"));
	// receiveShadows is a plain flag (no shadow system, so it has no visual
	// effect, but reads/writes behave); mesh.registerAfterRender is unmodeled.
	h->set("receiveShadows", value{false});
	h->set("registerAfterRender", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "registerAfterRender"));

	// getBoundingInfo(): local + world axis-aligned bounds, refreshed on demand.
	h->set("getBoundingInfo", value::function([W, ix](ctjs::context &, const std::vector<value> &) -> value {
		if (ix >= W->meshes.size()) { return value{}; }
		r3d::vec3 lmin = r3d::V3(0, 0, 0), lmax = r3d::V3(0, 0, 0);
		bool first = true;
		for (const r3d::vec3 & v : W->meshes[ix].geom.verts) {
			for (int k = 0; k < 3; ++k) {
				if (first) { lmin[k] = lmax[k] = v[k]; }
				else { lmin[k] = std::min(lmin[k], v[k]); lmax[k] = std::max(lmax[k], v[k]); }
			}
			first = false;
		}
		r3d::vec3 wmin, wmax;
		mesh_aabb(W, static_cast<int>(ix), wmin, wmax);
		auto box = objptr::make();
		box->set("minimum", make_vector3(lmin[0], lmin[1], lmin[2]));
		box->set("maximum", make_vector3(lmax[0], lmax[1], lmax[2]));
		box->set("minimumWorld", make_vector3(wmin[0], wmin[1], wmin[2]));
		box->set("maximumWorld", make_vector3(wmax[0], wmax[1], wmax[2]));
		box->set("centerWorld", make_vector3((wmin[0] + wmax[0]) * 0.5,
		                                     (wmin[1] + wmax[1]) * 0.5, (wmin[2] + wmax[2]) * 0.5));
		box->set("extendSize", make_vector3((lmax[0] - lmin[0]) * 0.5,
		                                    (lmax[1] - lmin[1]) * 0.5, (lmax[2] - lmin[2]) * 0.5));
		double rad = 0;
		for (int k = 0; k < 3; ++k) { rad = std::max(rad, (wmax[k] - wmin[k]) * 0.5); }
		auto sph = objptr::make();
		sph->set("radiusWorld", value{rad});
		sph->set("center", make_vector3((lmin[0] + lmax[0]) * 0.5,
		                                (lmin[1] + lmax[1]) * 0.5, (lmin[2] + lmax[2]) * 0.5));
		auto bi = objptr::make();
		bi->set("boundingBox", value{box});
		bi->set("boundingSphere", value{sph});
		bi->set("minimum", make_vector3(lmin[0], lmin[1], lmin[2]));
		bi->set("maximum", make_vector3(lmax[0], lmax[1], lmax[2]));
		return value{bi};
	}, "getBoundingInfo"));
	// refreshBoundingInfo(): bounds are computed on demand, so this just validates
	// the mesh and returns it (Babylon returns the mesh for chaining).
	h->set("refreshBoundingInfo", value::function([W, ix](ctjs::context & cx, const std::vector<value> &) -> value {
		return (ix < W->meshes.size()) ? value{W->meshes[ix].handle} : cx.current_this;
	}, "refreshBoundingInfo"));

	// computeWorldMatrix(force): return the current world matrix; if forced while
	// frozen, recapture the frozen matrix from the live transforms first.
	h->set("computeWorldMatrix", value::function([W, ix](ctjs::context &, const std::vector<value> & a) -> value {
		if (ix >= W->meshes.size()) { return value{}; }
		const bool force = !a.empty() && a[0].truthy();
		if (force && W->meshes[ix].frozen_world) {
			W->meshes[ix].frozen_matrix = mesh_world_matrix(W, static_cast<int>(ix), true);
		}
		return make_matrix(mesh_world_matrix(W, static_cast<int>(ix)));
	}, "computeWorldMatrix"));
	h->set("getWorldMatrix", value::function([W, ix](ctjs::context &, const std::vector<value> &) -> value {
		return (ix < W->meshes.size()) ? make_matrix(mesh_world_matrix(W, static_cast<int>(ix))) : value{};
	}, "getWorldMatrix"));
	// freezeWorldMatrix(): capture the world matrix now; the renderer then ignores
	// later position/rotation/scaling edits until unfreezeWorldMatrix().
	h->set("freezeWorldMatrix", value::function([W, ix](ctjs::context & cx, const std::vector<value> &) -> value {
		if (ix < W->meshes.size()) {
			W->meshes[ix].frozen_matrix = mesh_world_matrix(W, static_cast<int>(ix), true);
			W->meshes[ix].frozen_world = true;
		}
		return (ix < W->meshes.size()) ? value{W->meshes[ix].handle} : cx.current_this;
	}, "freezeWorldMatrix"));
	h->set("unfreezeWorldMatrix", value::function([W, ix](ctjs::context & cx, const std::vector<value> &) -> value {
		if (ix < W->meshes.size()) { W->meshes[ix].frozen_world = false; }
		return (ix < W->meshes.size()) ? value{W->meshes[ix].handle} : cx.current_this;
	}, "unfreezeWorldMatrix"));

	// setPivotPoint(vec3): rotation/scaling then pivot about this local point.
	h->set("setPivotPoint", value::function([W, ix](ctjs::context &, const std::vector<value> & a) -> value {
		if (ix < W->meshes.size()) {
			W->meshes[ix].pivot = read_vec3(arg_obj(a, 0), r3d::V3(0, 0, 0));
			W->meshes[ix].has_pivot = true;
		}
		return value{};
	}, "setPivotPoint"));
	h->set("getPivotPoint", value::function([W, ix](ctjs::context &, const std::vector<value> &) -> value {
		const r3d::vec3 p = ix < W->meshes.size() ? W->meshes[ix].pivot : r3d::V3(0, 0, 0);
		return make_vector3(p[0], p[1], p[2]);
	}, "getPivotPoint"));

	// bakeCurrentTransformIntoVertices(): fold the current world transform into the
	// geometry and reset position/rotation/scaling/pivot to identity.
	h->set("bakeCurrentTransformIntoVertices", value::function([W, ix](ctjs::context & cx, const std::vector<value> &) -> value {
		if (ix >= W->meshes.size()) { return cx.current_this; }
		mesh_rec & M = W->meshes[ix];
		const r3d::mat4 wm = mesh_world_matrix(W, static_cast<int>(ix), true);
		for (r3d::vec3 & v : M.geom.verts) {
			const r3d::vec4 t = r3d::xform(wm, v);
			v = r3d::V3(t[0], t[1], t[2]);
		}
		if (const objptr p = child_obj(M.handle, "position")) { p->set("x", value{0.0}); p->set("y", value{0.0}); p->set("z", value{0.0}); }
		if (const objptr r = child_obj(M.handle, "rotation")) { r->set("x", value{0.0}); r->set("y", value{0.0}); r->set("z", value{0.0}); }
		if (const objptr s = child_obj(M.handle, "scaling")) { s->set("x", value{1.0}); s->set("y", value{1.0}); s->set("z", value{1.0}); }
		M.has_pivot = false;
		M.frozen_world = false;
		return value{M.handle};
	}, "bakeCurrentTransformIntoVertices"));
}

// add a parsed glTF model's meshes + materials into a scene
inline void load_model(const worldptr & W, const objptr & scene, const gltf::model & mdl) {
	const int si = index_of(scene, "__scene");
	if (si < 0 || si >= static_cast<int>(W->scenes.size())) { return; }
	scene_rec & sc = W->scenes[static_cast<size_t>(si)];

	std::vector<objptr> mat_handles;
	for (const gltf::material & gm : mdl.materials) {
		objptr mh = objptr::make();
		mh->set("name", value{gm.name});
		mh->set("id", value{gm.name});
		mh->set("baseColor", make_color3(gm.base.r, gm.base.g, gm.base.b));
		mh->set("diffuseColor", make_color3(gm.base.r, gm.base.g, gm.base.b));
		mat_handles.push_back(mh);
		sc.materials.emplace_back(gm.name, mh);
	}
	// decode each glTF texture once (runtime); primitives share by index
	std::vector<std::shared_ptr<r3d::texture>> tex_decoded(mdl.textures.size());
	for (size_t i = 0; i < mdl.textures.size(); ++i) {
		tex_decoded[i] = r3d::decode_texture(mdl.textures[i].data(), mdl.textures[i].size());
	}
	for (const gltf::primitive & p : mdl.prims) {
		const int id = static_cast<int>(W->meshes.size());
		objptr h = objptr::make();
		h->set("name", value{p.node_name});
		h->set("id", value{p.node_name});
		h->set("__mesh", value{static_cast<double>(id)});
		h->set("position", make_vector3(0, 0, 0));
		h->set("rotation", make_vector3(0, 0, 0));
		h->set("scaling", make_vector3(1, 1, 1));
		std::shared_ptr<r3d::texture> ptex;
		if (p.material >= 0 && p.material < static_cast<int>(mat_handles.size())) {
			h->set("material", value{mat_handles[static_cast<size_t>(p.material)]});
			const int bt = mdl.materials[static_cast<size_t>(p.material)].base_tex;
			if (bt >= 0 && bt < static_cast<int>(tex_decoded.size())) { ptex = tex_decoded[static_cast<size_t>(bt)]; }
		} else {
			h->set("material", value{});
		}
		r3d::geo g{p.verts, p.tris, p.uvs};
		mesh_rec rec{std::move(g), h, true, false, true, {}, si};
		rec.tex = std::move(ptex);
		W->meshes.push_back(std::move(rec));
		sc.mesh_ids.push_back(id);
		decorate_mesh(W, h, id);
	}
	sc.bmin = mdl.bmin;
	sc.bmax = mdl.bmax;
	sc.has_bounds = mdl.ok;
}

inline value make_light(const worldptr & W, int type, std::string name, r3d::vec3 dir,
                        const objptr & scene) {
	const int id = static_cast<int>(W->lights.size());
	auto h = objptr::make();
	h->set("name", value{std::move(name)});
	h->set("direction", make_vector3(dir[0], dir[1], dir[2]));
	h->set("intensity", value{1.0});
	h->set("diffuse", make_color3(1, 1, 1));
	h->set("specular", make_color3(1, 1, 1));
	h->set("groundColor", make_color3(0, 0, 0));
	h->set("__light", value{static_cast<double>(id)});
	W->lights.push_back(light_rec{h, type});
	register_with_scene(W, scene, id, false);
	return value{h};
}

// register the drag-to-orbit mouse listeners for camera `id`; they act
// only after attachControl flips camera_rec.attached (avoids nested
// lambda captures — clang's -Wunused-lambda-capture dislikes those)
inline void register_orbit(const worldptr & W, dom_events & ev, int id) {
	const auto in_range = [W](int i) { return i >= 0 && i < static_cast<int>(W->cameras.size()); };
	ev.listeners["mousedown"].push_back(value::function([W, id, in_range](ctjs::context &, const std::vector<value> & a) -> value {
		if (in_range(id) && W->cameras[static_cast<size_t>(id)].attached) {
			const objptr e = arg_obj(a, 0);
			W->cam_dragging = true;
			W->cam_lastx = num_prop(e, "clientX", 0);
			W->cam_lasty = num_prop(e, "clientY", 0);
		}
		return value{};
	}, "_bjsDown"));
	ev.listeners["mouseup"].push_back(value::function([W](ctjs::context &, const std::vector<value> &) -> value {
		W->cam_dragging = false;
		return value{};
	}, "_bjsUp"));
	ev.listeners["mousemove"].push_back(value::function([W, id, in_range](ctjs::context &, const std::vector<value> & a) -> value {
		if (!W->cam_dragging || !in_range(id) || !W->cameras[static_cast<size_t>(id)].attached) { return value{}; }
		const objptr e = arg_obj(a, 0);
		const double x = num_prop(e, "clientX", W->cam_lastx);
		const double y = num_prop(e, "clientY", W->cam_lasty);
		const double dx = x - W->cam_lastx, dy = y - W->cam_lasty;
		W->cam_lastx = x;
		W->cam_lasty = y;
		const objptr ch = W->cameras[static_cast<size_t>(id)].handle;
		double beta = num_prop(ch, "beta", 1) - dy * 0.01;
		beta = beta < 0.05 ? 0.05 : (beta > std::numbers::pi - 0.05 ? std::numbers::pi - 0.05 : beta);
		ch->set("alpha", value{num_prop(ch, "alpha", 0) - dx * 0.01});
		ch->set("beta", value{beta});
		return value{};
	}, "_bjsMove"));
}

inline value make_camera_arc(const worldptr & W, dom_events & ev, std::string name, double alpha,
                             double beta, double radius, const objptr & tgt, const objptr & scene) {
	const int id = static_cast<int>(W->cameras.size());
	auto h = objptr::make();
	h->set("name", value{std::move(name)});
	h->set("alpha", value{alpha});
	h->set("beta", value{beta});
	h->set("radius", value{radius});
	h->set("lowerRadiusLimit", value{});
	h->set("upperRadiusLimit", value{});
	h->set("wheelPrecision", value{50.0});
	h->set("target", tgt ? value{tgt} : make_vector3(0, 0, 0));
	h->set("__camera", value{static_cast<double>(id)});
	h->set("setTarget", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		objptr s = self_of(cx);
		if (s && !a.empty() && a[0].is_object()) { s->set("target", a[0]); }
		return value{};
	}, "setTarget"));
	// drag to orbit: attachControl arms the listeners registered below
	h->set("attachControl", value::function([W, id](ctjs::context &, const std::vector<value> &) -> value {
		if (id >= 0 && id < static_cast<int>(W->cameras.size())) {
			W->cameras[static_cast<size_t>(id)].attached = true;
		}
		return value{};
	}, "attachControl"));
	h->set("detachControl", value::function([W, id](ctjs::context &, const std::vector<value> &) -> value {
		if (id >= 0 && id < static_cast<int>(W->cameras.size())) {
			W->cameras[static_cast<size_t>(id)].attached = false;
		}
		W->cam_dragging = false;
		return value{};
	}, "detachControl"));
	W->cameras.push_back(camera_rec{h, 0, false});
	register_orbit(W, ev, id);
	const int si = index_of(scene, "__scene");
	if (si >= 0 && si < static_cast<int>(W->scenes.size())) {
		W->scenes[static_cast<size_t>(si)].active_camera = id;
		if (scene) { scene->set("activeCamera", value{h}); }
	}
	return value{h};
}

inline value make_camera_free(const worldptr & W, std::string name, const objptr & pos,
                              const objptr & scene) {
	const int id = static_cast<int>(W->cameras.size());
	auto h = objptr::make();
	h->set("name", value{std::move(name)});
	h->set("position", pos ? value{pos} : make_vector3(0, 5, -10));
	h->set("target", make_vector3(0, 0, 0));
	h->set("rotation", make_vector3(0, 0, 0));
	h->set("mode", value{0.0}); // PERSPECTIVE; oldSchool sets ORTHOGRAPHIC + ortho* props
	h->set("fov", value{0.8});
	h->set("minZ", value{0.1});
	h->set("maxZ", value{1000.0});
	h->set("__camera", value{static_cast<double>(id)});
	h->set("setTarget", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		objptr s = self_of(cx);
		if (s && !a.empty() && a[0].is_object()) { s->set("target", a[0]); }
		return value{};
	}, "setTarget"));
	// setPosition(vec3): move the camera (FreeCamera-style eye position)
	h->set("setPosition", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const objptr s = self_of(cx);
		const objptr p = s ? child_obj(s, "position") : objptr{};
		if (p && !a.empty() && a[0].is_object()) {
			const r3d::vec3 v = read_vec3(a[0].as_object(), r3d::V3(0, 0, 0));
			p->set("x", value{v[0]}); p->set("y", value{v[1]}); p->set("z", value{v[2]});
		}
		return value{};
	}, "setPosition"));
	for (const char * nm : {"attachControl", "detachControl"}) {
		h->set(nm, value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, nm));
	}
	W->cameras.push_back(camera_rec{h, 1, false});
	const int si = index_of(scene, "__scene");
	if (si >= 0 && si < static_cast<int>(W->scenes.size())) {
		W->scenes[static_cast<size_t>(si)].active_camera = id;
		if (scene) { scene->set("activeCamera", value{h}); }
	}
	return value{h};
}

inline value make_scene(const worldptr & W, dom_events & ev) {
	const int id = static_cast<int>(W->scenes.size());
	auto h = objptr::make();
	h->set("__scene", value{static_cast<double>(id)});
	h->set("clearColor", make_color4(0.2, 0.2, 0.3, 1.0));
	h->set("ambientColor", make_color3(0, 0, 0));
	h->set("activeCamera", value{});
	h->set("render", value::function([W, id, &ev](ctjs::context & cx, const std::vector<value> &) -> value {
		ev.cx = &cx;
		// refresh scene.deltaTime (scene.meshes is a LIVE getter, see below), then
		// run the frame's onBeforeRender observers (the game's whole per-frame
		// logic) and draw
		if (id >= 0 && id < static_cast<int>(W->scenes.size())) {
			W->scenes[static_cast<std::size_t>(id)].handle->set("deltaTime", value{W->last_dt_ms});
		}
		fire_before_render(W, id, cx);
		fire_action_managers(W, id, cx);
		do_render(W, id);
		fire_after_render(W, id, cx);
		return value{};
	}, "render"));
	// scene.onBeforeRenderObservable is REAL - it drives per-frame game logic
	h->set("onBeforeRenderObservable", make_observable(W, id, false));
	// scene.meshes is a LIVE getter (not a per-frame snapshot): it re-reads the
	// scene's currently non-disposed mesh handles on EVERY access. This is load-
	// bearing for teardown - the game runs, inside onBeforeRender,
	//     while (scene.meshes.length) scene.meshes[0].dispose();
	// and dispose() removes the id from sc.mesh_ids. A snapshot array would never
	// shrink mid-loop, so the loop would spin forever (hanging the whole app).
	ctjs::attach_accessor(*h, "meshes", 'g', value::function(
	    [W, id](ctjs::context &, const std::vector<value> &) -> value {
		    std::vector<value> ms;
		    if (id >= 0 && id < static_cast<int>(W->scenes.size())) {
			    scene_rec & sc = W->scenes[static_cast<std::size_t>(id)];
			    for (int mi : sc.mesh_ids) {
				    if (mi >= 0 && mi < static_cast<int>(W->meshes.size()) &&
				        !W->meshes[static_cast<std::size_t>(mi)].disposed) {
					    ms.push_back(value{W->meshes[static_cast<std::size_t>(mi)].handle});
				    }
			    }
		    }
		    return value::array(std::move(ms));
	    }, "get meshes"));
	// data props the game sets/reads
	h->set("collisionsEnabled", value{false});
	h->set("gravity", make_vector3(0, -9.81, 0));
	h->set("fogEnabled", value{false});
	h->set("fogMode", value{0.0});
	h->set("fogColor", make_color3(0, 0, 0));
	h->set("fogStart", value{0.0});
	h->set("fogEnd", value{0.0});
	h->set("fogDensity", value{0.1});
	h->set("deltaTime", value{16.6});
	h->set("actionManager", value{});
	// registerBeforeRender/registerAfterRender are REAL - the legacy (pre-Observable)
	// per-frame hooks; they push into the same sinks scene.render() fires.
	h->set("registerBeforeRender", value::function([W, id](ctjs::context &, const std::vector<value> & a) -> value {
		if (!a.empty() && a[0].is_function() && id >= 0 && id < static_cast<int>(W->scenes.size())) {
			W->scenes[static_cast<size_t>(id)].before_render.push_back({W->next_obs++, a[0]});
		}
		return value{};
	}, "registerBeforeRender"));
	h->set("registerAfterRender", value::function([W, id](ctjs::context &, const std::vector<value> & a) -> value {
		if (!a.empty() && a[0].is_function() && id >= 0 && id < static_cast<int>(W->scenes.size())) {
			W->scenes[static_cast<size_t>(id)].after_render.push_back({W->next_obs++, a[0]});
		}
		return value{};
	}, "registerAfterRender"));
	// onAfterRenderObservable mirrors registerAfterRender (both feed after_render)
	{
		auto obs = objptr::make();
		obs->set("add", value::function([W, id](ctjs::context &, const std::vector<value> & a) -> value {
			if (a.empty() || !a[0].is_function() || id < 0 || id >= static_cast<int>(W->scenes.size())) { return value{}; }
			const int oid = W->next_obs++;
			W->scenes[static_cast<size_t>(id)].after_render.push_back({oid, a[0]});
			auto ob = objptr::make();
			ob->set("__obs_id", value{static_cast<double>(oid)});
			return value{ob};
		}, "add"));
		obs->set("remove", value::function([](ctjs::context &, const std::vector<value> &) { return value{true}; }, "remove"));
		obs->set("clear", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "clear"));
		h->set("onAfterRenderObservable", value{obs});
	}
	// freezeActiveMeshes/unfreezeActiveMeshes: a Babylon optimization that caches
	// the active-mesh list. We rebuild the draw list each frame regardless, so this
	// only records the flag (scene.__activeMeshesFrozen) for API fidelity.
	h->set("freezeActiveMeshes", value::function([W, id](ctjs::context &, const std::vector<value> &) -> value {
		if (id >= 0 && id < static_cast<int>(W->scenes.size())) { W->scenes[static_cast<size_t>(id)].active_meshes_frozen = true; }
		return value{};
	}, "freezeActiveMeshes"));
	h->set("unfreezeActiveMeshes", value::function([W, id](ctjs::context &, const std::vector<value> &) -> value {
		if (id >= 0 && id < static_cast<int>(W->scenes.size())) { W->scenes[static_cast<size_t>(id)].active_meshes_frozen = false; }
		return value{};
	}, "unfreezeActiveMeshes"));
	// clearCachedVertexData: on real Babylon this frees CPU geometry after GPU
	// upload. The software rasterizer needs the vertices every frame, so there is
	// nothing to free - a genuine no-op, kept so scripts calling it don't throw.
	h->set("clearCachedVertexData", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "clearCachedVertexData"));
	// getUniqueId(): a real monotonic id (scripts key maps on it)
	h->set("getUniqueId", value::function([W](ctjs::context &, const std::vector<value> &) -> value {
		return value{static_cast<double>(W->next_uid++)};
	}, "getUniqueId"));
	// commonly-probed no-ops so real scripts don't throw
	for (const char * nm : {"beforeRender", "dispose", "attachControl", "detachControl"}) {
		h->set(nm, value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, nm));
	}
	// getEngine() hands back the BABYLON.Engine handle (scene.getEngine().getDeltaTime())
	h->set("getEngine", value::function([W](ctjs::context &, const std::vector<value> &) -> value {
		return W->engine_handle ? value{W->engine_handle} : value{};
	}, "getEngine"));

	// --- model-viewer helpers (glTF loading path)
	h->set("getMaterialById", value::function([W, id](ctjs::context &, const std::vector<value> & a) -> value {
		if (id < 0 || id >= static_cast<int>(W->scenes.size()) || a.empty()) { return value{}; }
		const std::string name = a[0].to_string();
		for (const auto & mm : W->scenes[static_cast<size_t>(id)].materials) {
			if (mm.first == name) { return value{mm.second}; }
		}
		return value{};
	}, "getMaterialById"));
	h->set("getMaterialByName", h->find("getMaterialById") ? *h->find("getMaterialById") : value{});
	h->set("createDefaultCamera", value::function([W, id, &ev](ctjs::context &, const std::vector<value> &) -> value {
		if (id < 0 || id >= static_cast<int>(W->scenes.size())) { return value{}; }
		scene_rec & sc = W->scenes[static_cast<size_t>(id)];
		r3d::vec3 c = r3d::V3(0, 0, 0);
		double rad = 2.0;
		if (sc.has_bounds) {
			c = r3d::V3((sc.bmin[0] + sc.bmax[0]) * 0.5, (sc.bmin[1] + sc.bmax[1]) * 0.5,
			            (sc.bmin[2] + sc.bmax[2]) * 0.5);
			rad = 0.001;
			for (int k = 0; k < 3; ++k) { rad = std::max(rad, sc.bmax[k] - sc.bmin[k]); }
		}
		const objptr target = make_vector3(c[0], c[1], c[2]).as_object();
		return make_camera_arc(W, ev, "default_camera", -std::numbers::pi / 2, std::numbers::pi / 2.5, rad * 2.2, target, sc.handle);
	}, "createDefaultCamera"));
	h->set("createDefaultLight", value::function([W, id](ctjs::context &, const std::vector<value> &) -> value {
		if (id < 0 || id >= static_cast<int>(W->scenes.size())) { return value{}; }
		return make_light(W, 0, "default_light", r3d::V3(0, 1, 0), W->scenes[static_cast<size_t>(id)].handle);
	}, "createDefaultLight"));
	for (const char * nm : {"createDefaultSkybox", "createDefaultEnvironment"}) {
		h->set(nm, value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, nm));
	}
	// debug inspector: no-ops satisfying `await scene.debugLayer.show(...).select(...)`
	{
		object_t dbg;
		dbg.set("show", value::function([](ctjs::context &, const std::vector<value> &) -> value {
			object_t d;
			d.set("select", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "select"));
			return ctjs::make_promise(value::object(std::move(d)), false);
		}, "show"));
		dbg.set("hide", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "hide"));
		h->set("debugLayer", value::object(std::move(dbg)));
	}

	scene_rec srec;
	srec.handle = h;
	W->scenes.push_back(std::move(srec));
	return value{h};
}

inline value make_engine(const worldptr & W, dom_events & ev, const std::vector<value> & args) {
	if (!args.empty()) {
		if (ctbrowser::node * n = ev.node_of(args[0])) { W->target = n; }
	}
	auto h = objptr::make();
	W->engine_handle = h; // scene.getEngine() hands this back
	h->set("runRenderLoop", value::function([W, &ev](ctjs::context & cx, const std::vector<value> & a) -> value {
		ev.cx = &cx;
		if (!a.empty() && a[0].is_function()) { W->render_cb = a[0]; }
		if (!W->loop_active) {
			W->loop_active = true;
			std::weak_ptr<world> wk = W;
			W->loop_wrapper = value::function(
			    [wk, &ev](ctjs::context & cx2, const std::vector<value> & b) -> value {
				    auto w = wk.lock();
				    if (!w) { return value{}; } // world gone -> stop the loop
				    ev.cx = &cx2;
				    const double now = b.empty() ? ev.now_ms : b[0].to_number();
				    w->last_dt_ms = (w->prev_ms == 0) ? 16.6 : (now - w->prev_ms);
				    w->prev_ms = now;
				    if (w->render_cb.is_function()) { ctjs::call_value(cx2, w->render_cb, {}); }
				    ev.raf.push_back(w->loop_wrapper); // re-register for next frame
				    return value{};
			    }, "babylonRenderLoop");
			ev.raf.push_back(W->loop_wrapper);
		}
		return value{};
	}, "runRenderLoop"));
	h->set("stopRenderLoop", value::function([W](ctjs::context &, const std::vector<value> &) -> value {
		W->render_cb = value{};
		return value{};
	}, "stopRenderLoop"));
	h->set("getDeltaTime", value::function([W](ctjs::context &, const std::vector<value> &) {
		return value{W->last_dt_ms};
	}, "getDeltaTime"));
	h->set("getFps", value::function([W](ctjs::context &, const std::vector<value> &) {
		return value{W->last_dt_ms > 0 ? 1000.0 / W->last_dt_ms : 60.0};
	}, "getFps"));
	// engine.resize(): match the canvas drawing buffer to the current
	// viewport (window inner size) so the 3D view fills the resized window
	h->set("resize", value::function([W, &ev](ctjs::context &, const std::vector<value> &) -> value {
		if (W->target != nullptr && ev.viewport_w > 0 && ev.viewport_h > 0 &&
		    (W->target->canvas_w != ev.viewport_w || W->target->canvas_h != ev.viewport_h)) {
			ctbrowser::node * n = W->target;
			n->canvas_w = ev.viewport_w;
			n->canvas_h = ev.viewport_h;
			n->pixels.assign(static_cast<size_t>(n->canvas_w) * static_cast<size_t>(n->canvas_h), 0xFF000000u);
		}
		return value{};
	}, "resize"));
	// the render buffer size (Environment reads these for the ortho camera)
	h->set("getRenderWidth", value::function([W, &ev](ctjs::context &, const std::vector<value> &) {
		return value{static_cast<double>(W->target != nullptr ? W->target->canvas_w : ev.viewport_w)};
	}, "getRenderWidth"));
	h->set("getRenderHeight", value::function([W, &ev](ctjs::context &, const std::vector<value> &) {
		return value{static_cast<double>(W->target != nullptr ? W->target->canvas_h : ev.viewport_h)};
	}, "getRenderHeight"));
	h->set("onResizeObservable", make_dead_observable());
	h->set("dispose", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "dispose"));
	h->set("loadingUIText", value{std::string{"Loading..."}});
	// displayLoadingUI(): paint a dark backdrop + the loading text into the canvas
	// (the next scene.render() overwrites it, which is exactly hideLoadingUI's job).
	h->set("displayLoadingUI", value::function([W](ctjs::context & cx, const std::vector<value> &) -> value {
		ctbrowser::node * n = W->target;
		if (n == nullptr || n->pixels.empty()) { return value{}; }
		const int cw = n->canvas_w, ch = n->canvas_h;
		std::string text = "Loading...";
		if (const objptr self = self_of(cx)) {
			if (const value * t = self->find("loadingUIText"); t != nullptr && !t->is_undefined()) { text = t->to_string(); }
		}
		for (uint32_t & px : n->pixels) { px = 0xFF060606u; }
		const int scale = cw >= 200 ? 2 : 1;
		const int tw = static_cast<int>(text.size()) * 8 * scale;
		overlay_text(n->pixels.data(), cw, ch, (cw - tw) / 2, ch / 2 - 4 * scale, text, scale, 0xFFCCCCCCu);
		return value{};
	}, "displayLoadingUI"));
	// hideLoadingUI(): the loading frame lives in the canvas until the next render
	// paints over it, so nothing to tear down here.
	h->set("hideLoadingUI", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "hideLoadingUI"));
	{
		object_t hi;
		hi.set("isMobile", value{false});
		h->set("hostInformation", value::object(std::move(hi)));
	}
	return value{h};
}

// --- geometry option readers for MeshBuilder
inline double opt(const objptr & o, const char * k, double dflt) { return num_prop(o, k, dflt); }

inline value build_babylon(const worldptr & W, dom_events & ev, image_store & images) {
	auto B = objptr::make();

	// AppendSceneAsync(url, scene, opts): resolve the .glb from the
	// embedded-asset registry (same path as fetch), parse it, add its
	// meshes/materials to the scene; returns a settled (resolved) promise.
	auto append_scene = value::function([W, &images](ctjs::context &, const std::vector<value> & a) -> value {
		const std::string url = a.empty() ? "" : a[0].to_string();
		const objptr scene = arg_obj(a, 1);
		const ctbrowser::embedded_asset * hit = ctbrowser::find_asset(images.embedded, url);
		if (hit == nullptr) {
			return ctjs::make_promise(ctjs::make_error("Error", "glTF not embedded (build with --fetch-allow): " + url), true);
		}
		const gltf::model mdl = gltf::parse_glb(reinterpret_cast<const unsigned char *>(hit->data), hit->size);
		if (mdl.ok) { load_model(W, scene, mdl); }
		return ctjs::make_promise(value{}, false);
	}, "AppendSceneAsync");
	B->set("AppendSceneAsync", append_scene);
	B->set("appendSceneAsync", append_scene);
	B->set("ImportMeshAsync", append_scene);
	// CubeTexture / environment: stubbed (no IBL) so scripts don't throw
	{
		object_t ct;
		ct.set("CreateFromPrefilteredData", value::function([](ctjs::context &, const std::vector<value> &) -> value {
			object_t o; o.set("__cubeTexture", value{true}); return value::object(std::move(o));
		}, "CreateFromPrefilteredData"));
		value CubeTexture = value::function([](ctjs::context &, const std::vector<value> &) -> value {
			object_t o; o.set("__cubeTexture", value{true}); return value::object(std::move(o));
		}, "CubeTexture");
		set_static(CubeTexture, "CreateFromPrefilteredData", *ct.find("CreateFromPrefilteredData"));
		B->set("CubeTexture", CubeTexture);
	}

	// Vector3 (callable + statics)
	value Vector3 = value::function([](ctjs::context &, const std::vector<value> & a) -> value {
		return make_vector3(arg_num(a, 0, 0), arg_num(a, 1, 0), arg_num(a, 2, 0));
	}, "Vector3");
	set_static(Vector3, "Zero", value::function([](ctjs::context &, const std::vector<value> &) { return make_vector3(0, 0, 0); }, "Zero"));
	set_static(Vector3, "One", value::function([](ctjs::context &, const std::vector<value> &) { return make_vector3(1, 1, 1); }, "One"));
	set_static(Vector3, "Up", value::function([](ctjs::context &, const std::vector<value> &) { return make_vector3(0, 1, 0); }, "Up"));
	set_static(Vector3, "Down", value::function([](ctjs::context &, const std::vector<value> &) { return make_vector3(0, -1, 0); }, "Down"));
	set_static(Vector3, "Forward", value::function([](ctjs::context &, const std::vector<value> &) { return make_vector3(0, 0, 1); }, "Forward"));
	set_static(Vector3, "Backward", value::function([](ctjs::context &, const std::vector<value> &) { return make_vector3(0, 0, -1); }, "Backward"));
	set_static(Vector3, "Right", value::function([](ctjs::context &, const std::vector<value> &) { return make_vector3(1, 0, 0); }, "Right"));
	set_static(Vector3, "Left", value::function([](ctjs::context &, const std::vector<value> &) { return make_vector3(-1, 0, 0); }, "Left"));
	B->set("Vector3", Vector3);

	value Color3 = value::function([](ctjs::context &, const std::vector<value> & a) -> value {
		return make_color3(arg_num(a, 0, 0), arg_num(a, 1, 0), arg_num(a, 2, 0));
	}, "Color3");
	set_static(Color3, "White", value::function([](ctjs::context &, const std::vector<value> &) { return make_color3(1, 1, 1); }, "White"));
	set_static(Color3, "Black", value::function([](ctjs::context &, const std::vector<value> &) { return make_color3(0, 0, 0); }, "Black"));
	set_static(Color3, "Red", value::function([](ctjs::context &, const std::vector<value> &) { return make_color3(1, 0, 0); }, "Red"));
	set_static(Color3, "Green", value::function([](ctjs::context &, const std::vector<value> &) { return make_color3(0, 1, 0); }, "Green"));
	set_static(Color3, "Blue", value::function([](ctjs::context &, const std::vector<value> &) { return make_color3(0, 0, 1); }, "Blue"));
	B->set("Color3", Color3);

	value Color4 = value::function([](ctjs::context &, const std::vector<value> & a) -> value {
		return make_color4(arg_num(a, 0, 0), arg_num(a, 1, 0), arg_num(a, 2, 0), arg_num(a, 3, 1));
	}, "Color4");
	B->set("Color4", Color4);

	B->set("Engine", value::function([W, &ev](ctjs::context &, const std::vector<value> & a) -> value {
		return make_engine(W, ev, a);
	}, "Engine"));
	B->set("Scene", value::function([W, &ev](ctjs::context &, const std::vector<value> &) -> value {
		return make_scene(W, ev);
	}, "Scene"));
	B->set("ArcRotateCamera", value::function([W, &ev](ctjs::context &, const std::vector<value> & a) -> value {
		return make_camera_arc(W, ev, a.empty() ? "" : a[0].to_string(), arg_num(a, 1, 0), arg_num(a, 2, 1),
		                       arg_num(a, 3, 10), arg_obj(a, 4), arg_obj(a, 5));
	}, "ArcRotateCamera"));
	B->set("FreeCamera", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		return make_camera_free(W, a.empty() ? "" : a[0].to_string(), arg_obj(a, 1), arg_obj(a, 2));
	}, "FreeCamera"));
	B->set("UniversalCamera", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		return make_camera_free(W, a.empty() ? "" : a[0].to_string(), arg_obj(a, 1), arg_obj(a, 2));
	}, "UniversalCamera"));
	B->set("HemisphericLight", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		return make_light(W, 0, a.empty() ? "" : a[0].to_string(), read_vec3(arg_obj(a, 1), r3d::V3(0, 1, 0)), arg_obj(a, 2));
	}, "HemisphericLight"));
	B->set("DirectionalLight", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		return make_light(W, 1, a.empty() ? "" : a[0].to_string(), read_vec3(arg_obj(a, 1), r3d::V3(0, -1, 0)), arg_obj(a, 2));
	}, "DirectionalLight"));
	B->set("PointLight", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		return make_light(W, 1, a.empty() ? "" : a[0].to_string(), read_vec3(arg_obj(a, 1), r3d::V3(0, -1, 0)), arg_obj(a, 2));
	}, "PointLight"));
	B->set("StandardMaterial", value::function([](ctjs::context &, const std::vector<value> & a) -> value {
		return make_material(a.empty() ? "" : a[0].to_string());
	}, "StandardMaterial"));

	// MeshBuilder (object of factory statics)
	object_t mb;
	mb.set("CreateBox", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		const objptr o = arg_obj(a, 1);
		const double sz = opt(o, "size", 1.0);
		return make_mesh(W, r3d::make_box(sz), a.empty() ? "" : a[0].to_string(), true, arg_obj(a, 2));
	}, "CreateBox"));
	mb.set("CreateSphere", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		const objptr o = arg_obj(a, 1);
		return make_mesh(W, r3d::make_sphere(opt(o, "diameter", 1.0), static_cast<int>(opt(o, "segments", 16))),
		                 a.empty() ? "" : a[0].to_string(), true, arg_obj(a, 2));
	}, "CreateSphere"));
	mb.set("CreateGround", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		const objptr o = arg_obj(a, 1);
		return make_mesh(W, r3d::make_ground(opt(o, "width", 1.0), opt(o, "height", 1.0)),
		                 a.empty() ? "" : a[0].to_string(), false, arg_obj(a, 2));
	}, "CreateGround"));
	mb.set("CreateCylinder", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		const objptr o = arg_obj(a, 1);
		return make_mesh(W, r3d::make_cylinder(opt(o, "height", 2.0), opt(o, "diameter", 1.0), static_cast<int>(opt(o, "tessellation", 24))),
		                 a.empty() ? "" : a[0].to_string(), true, arg_obj(a, 2));
	}, "CreateCylinder"));
	B->set("MeshBuilder", value::object(std::move(mb)));

	// legacy Mesh.Create* (positional args)
	object_t mesh;
	mesh.set("CreateBox", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		return make_mesh(W, r3d::make_box(arg_num(a, 1, 1)), a.empty() ? "" : a[0].to_string(), true, arg_obj(a, 2));
	}, "CreateBox"));
	mesh.set("CreateSphere", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		return make_mesh(W, r3d::make_sphere(arg_num(a, 2, 1), static_cast<int>(arg_num(a, 1, 16))),
		                 a.empty() ? "" : a[0].to_string(), true, arg_obj(a, 3));
	}, "CreateSphere"));
	mesh.set("CreateGround", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		return make_mesh(W, r3d::make_ground(arg_num(a, 1, 1), arg_num(a, 2, 1)),
		                 a.empty() ? "" : a[0].to_string(), false, arg_obj(a, 4));
	}, "CreateGround"));
	B->set("Mesh", value::object(std::move(mesh)));

	// --- Scalar: gameplay math (Lerp/Clamp/RandomRange, deterministic PRNG)
	{
		object_t sc;
		sc.set("Lerp", value::function([](ctjs::context &, const std::vector<value> & a) {
			const double x = arg_num(a, 0, 0), y = arg_num(a, 1, 0), t = arg_num(a, 2, 0);
			return value{x + (y - x) * t};
		}, "Lerp"));
		sc.set("Clamp", value::function([](ctjs::context &, const std::vector<value> & a) {
			const double v = arg_num(a, 0, 0), lo = arg_num(a, 1, 0), hi = arg_num(a, 2, 1);
			return value{v < lo ? lo : v > hi ? hi : v};
		}, "Clamp"));
		sc.set("RandomRange", value::function([W](ctjs::context &, const std::vector<value> & a) {
			const double lo = arg_num(a, 0, 0), hi = arg_num(a, 1, 1);
			W->rng = W->rng * 1664525u + 1013904223u;
			const double r = static_cast<double>(W->rng >> 8) / 16777216.0;
			return value{lo + (hi - lo) * r};
		}, "RandomRange"));
		B->set("Scalar", value::object(std::move(sc)));
	}
	// --- Axis / Space
	{
		object_t ax;
		ax.set("X", make_vector3(1, 0, 0));
		ax.set("Y", make_vector3(0, 1, 0));
		ax.set("Z", make_vector3(0, 0, 1));
		B->set("Axis", value::object(std::move(ax)));
		object_t sp;
		sp.set("LOCAL", value{0.0});
		sp.set("WORLD", value{1.0});
		sp.set("BONE", value{2.0});
		B->set("Space", value::object(std::move(sp)));
	}
	// --- Camera: projection-mode constants (base class the game reads statics off)
	{
		value Camera = value::function([](ctjs::context &, const std::vector<value> &) -> value {
			return value{objptr::make()};
		}, "Camera");
		set_static(Camera, "PERSPECTIVE_CAMERA", value{0.0});
		set_static(Camera, "ORTHOGRAPHIC_CAMERA", value{1.0});
		B->set("Camera", Camera);
	}
	// --- Sound: no-op audio, but its onLoaded callback FIRES (asset counters
	// gate the game's start on it) - scheduled as a 0ms timer so it is async
	B->set("Sound", value::function([&ev](ctjs::context &, const std::vector<value> & a) -> value {
		auto o = objptr::make();
		const std::string url = a.size() > 1 ? a[1].to_string() : std::string{};
		const bool loop = a.size() > 4 && a[4].is_object() && num_prop(a[4].as_object(), "loop", 0) != 0;
		o->set("name", value{a.empty() ? std::string{} : a[0].to_string()});
		o->set("isPlaying", value{false});
		o->set("isReady", value{true});
		o->set("loop", value{loop});
		// route through the shell's audio hook (empty in headless builds). The
		// live track handle is shared between play() and stop() (no rc cycle).
		auto handle = std::make_shared<int>(0);
		o->set("play", value::function([&ev, url, loop, handle](ctjs::context &, const std::vector<value> &) -> value {
			if (ev.play_audio) { *handle = ev.play_audio(url, loop); }
			return value{};
		}, "play"));
		o->set("stop", value::function([&ev, handle](ctjs::context &, const std::vector<value> &) -> value {
			if (ev.stop_audio && *handle != 0) { ev.stop_audio(*handle); }
			*handle = 0;
			return value{};
		}, "stop"));
		o->set("setVolume", value::function([&ev](ctjs::context &, const std::vector<value> & sa) -> value {
			if (ev.set_audio_volume && !sa.empty()) { ev.set_audio_volume(static_cast<float>(sa[0].to_number())); }
			return value{};
		}, "setVolume"));
		// pause/dispose stop playback through the mixer (we have no true pause, so
		// pause == stop). Spatial audio (setPlaybackRate/attachToMesh/setPosition)
		// is unmodeled - the mixer is non-positional.
		const auto stopper = [&ev, handle](ctjs::context &, const std::vector<value> &) -> value {
			if (ev.stop_audio && *handle != 0) { ev.stop_audio(*handle); }
			*handle = 0;
			return value{};
		};
		o->set("pause", value::function(stopper, "pause"));
		o->set("dispose", value::function(stopper, "dispose"));
		for (const char * nm : {"setPlaybackRate", "attachToMesh", "setPosition"}) {
			o->set(nm, value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, nm));
		}
		if (a.size() > 3 && a[3].is_function()) {
			ev.timers.push_back({++ev.timer_seq, ev.now_ms, 0, false, a[3]});
		}
		return value{o};
	}, "Sound"));
	// --- SceneLoader.ImportMeshAsync(meshNames, rootUrl, filename, scene):
	// resolves {meshes:[__root__, model]}. The real GLB (rootUrl+filename) is
	// loaded from the embedded registry when present - its primitives merged into
	// ONE mesh (the game clones meshes[1] per alien) - else a box placeholder.
	{
		object_t sl;
		sl.set("ImportMeshAsync", value::function([W, &images](ctjs::context &, const std::vector<value> & a) -> value {
			const std::string root_url = a.size() > 1 ? a[1].to_string() : std::string{};
			const std::string fname = a.size() > 2 ? a[2].to_string() : std::string{};
			const std::string url = root_url + fname; // e.g. "/assets/models/Alien_1.glb"
			std::string base = fname;                 // display name: strip dir + ext
			if (const std::size_t s = base.find_last_of("/\\"); s != std::string::npos) { base = base.substr(s + 1); }
			if (const std::size_t d = base.rfind('.'); d != std::string::npos) { base = base.substr(0, d); }
			const objptr scene = arg_obj(a, 3);
			const value root = make_mesh(W, r3d::make_box(0.01), "__root__", true, scene);
			value body;
			const ctbrowser::embedded_asset * hit = ctbrowser::find_asset(images.embedded, url);
			gltf::model mdl;
			if (hit != nullptr) { mdl = gltf::parse_glb(hit->data, hit->size); }
			if (mdl.ok && !mdl.prims.empty()) {
				r3d::geo merged;
				r3d::rgba col{1, 1, 1, 1};
				std::shared_ptr<r3d::texture> mtex; // first baseColor texture found
				for (const gltf::primitive & p : mdl.prims) {
					const int off = static_cast<int>(merged.verts.size());
					for (const r3d::vec3 & v : p.verts) { merged.verts.push_back(v); }
					for (const std::array<int, 3> & t : p.tris) {
						merged.tris.push_back({t[0] + off, t[1] + off, t[2] + off});
					}
					// keep UVs parallel to verts (pad missing prims with zeros)
					if (p.uvs.size() == p.verts.size()) {
						for (const r3d::vec2 & uv : p.uvs) { merged.uvs.push_back(uv); }
					} else {
						for (size_t k = 0; k < p.verts.size(); ++k) { merged.uvs.push_back(r3d::V2(0, 0)); }
					}
					if (p.material >= 0 && p.material < static_cast<int>(mdl.materials.size())) {
						col = mdl.materials[static_cast<size_t>(p.material)].base;
						const int bt = mdl.materials[static_cast<size_t>(p.material)].base_tex;
						if (!mtex && bt >= 0 && bt < static_cast<int>(mdl.textures.size())) {
							mtex = r3d::decode_texture(mdl.textures[static_cast<size_t>(bt)].data(),
							                           mdl.textures[static_cast<size_t>(bt)].size());
						}
					}
				}
				if (!mtex) { merged.uvs.clear(); } // no texture => flat-colour path
				const int bid = static_cast<int>(W->meshes.size()); // id make_mesh will assign
				body = make_mesh(W, std::move(merged), base, true, scene);
				if (mtex && bid < static_cast<int>(W->meshes.size())) {
					W->meshes[static_cast<size_t>(bid)].tex = mtex;
				}
				auto mh = objptr::make(); // a material so it isn't the default gray
				mh->set("diffuseColor", make_color3(col.r, col.g, col.b));
				mh->set("baseColor", make_color3(col.r, col.g, col.b));
				if (body.is_object()) { body.as_object()->set("material", value{mh}); }
			} else {
				body = make_mesh(W, r3d::make_box(2.0), base, true, scene); // placeholder
			}
			auto res = objptr::make();
			res->set("meshes", value::array({root, body}));
			res->set("particleSystems", value::array({}));
			res->set("skeletons", value::array({}));
			res->set("animationGroups", value::array({}));
			res->set("transformNodes", value::array({}));
			return ctjs::make_promise(value{res}, false);
		}, "ImportMeshAsync"));
		sl.set("AppendAsync", *B->find("AppendSceneAsync"));
		B->set("SceneLoader", value::object(std::move(sl)));
	}
	// --- AssetContainer: a .meshes array the game pushes its model ORIGINALS to
	// (parked off-screen, cloned each round). removeAllFromScene takes those
	// meshes OUT of the scene's render/dispose list - LOAD-BEARING across games:
	// clearLevel() disposes everything still in scene.meshes on game over, so if
	// the originals stayed in the scene they'd be destroyed and the next game
	// would clone empty meshes (invisible aliens/player). Detaching keeps them in
	// world.meshes (still cloneable) but out of harm's way.
	{
		const auto move_container = [W](ctjs::context & cx, bool add) {
			const objptr self = self_of(cx);
			if (!self) { return; }
			const value * mv = self->find("meshes");
			if (mv == nullptr || !mv->is_array()) { return; }
			for (const value & m : *mv->as_array()) {
				const int mi = m.is_object() ? index_of(m.as_object(), "__mesh") : -1;
				if (mi < 0 || mi >= static_cast<int>(W->meshes.size())) { continue; }
				const int sid = W->meshes[static_cast<size_t>(mi)].scene_id;
				if (sid < 0 || sid >= static_cast<int>(W->scenes.size())) { continue; }
				auto & ids = W->scenes[static_cast<size_t>(sid)].mesh_ids;
				if (add) {
					if (std::find(ids.begin(), ids.end(), mi) == ids.end()) { ids.push_back(mi); }
				} else {
					ids.erase(std::remove(ids.begin(), ids.end(), mi), ids.end());
				}
			}
		};
		B->set("AssetContainer", value::function([move_container](ctjs::context &, const std::vector<value> &) -> value {
			auto o = objptr::make();
			o->set("meshes", value::array({}));
			o->set("materials", value::array({}));
			o->set("textures", value::array({}));
			o->set("removeAllFromScene", value::function([move_container](ctjs::context & cx, const std::vector<value> &) -> value {
				move_container(cx, false);
				return value{};
			}, "removeAllFromScene"));
			o->set("addAllToScene", value::function([move_container](ctjs::context & cx, const std::vector<value> &) -> value {
				move_container(cx, true);
				return value{};
			}, "addAllToScene"));
			o->set("dispose", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "dispose"));
			return value{o};
		}, "AssetContainer"));
	}
	// --- Sprite / SpriteManager: cosmetic (starfield); constructed, not rendered
	B->set("SpriteManager", value::function([](ctjs::context &, const std::vector<value> &) -> value {
		auto o = objptr::make();
		o->set("sprites", value::array({}));
		for (const char * nm : {"dispose", "render"}) {
			o->set(nm, value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, nm));
		}
		return value{o};
	}, "SpriteManager"));
	B->set("Sprite", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		auto o = objptr::make();
		o->set("name", value{a.empty() ? std::string{} : a[0].to_string()});
		o->set("position", make_vector3(0, 0, 0));
		o->set("color", make_color4(1, 1, 1, 1));
		o->set("size", value{1.0});
		o->set("width", value{1.0});
		o->set("height", value{1.0});
		o->set("angle", value{0.0});
		o->set("isVisible", value{true});
		// dispose hides it from the overlay (render_sprites skips !isVisible)
		o->set("dispose", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
			if (const objptr s = self_of(cx)) { s->set("isVisible", value{false}); }
			return value{};
		}, "dispose"));
		for (const char * nm : {"playAnimation", "stopAnimation"}) {
			o->set(nm, value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, nm));
		}
		W->sprites.push_back(o); // drawn as a 2D overlay by render_sprites
		return value{o};
	}, "Sprite"));
	// --- GlowLayer: REAL additive-bloom post-process. `new GlowLayer(name, scene,
	// {mainTextureFixedSize,...})`; do_render blurs bright pixels into a halo scaled
	// by .intensity (live). Per-mesh include/exclude masking stays a no-op (the
	// whole framebuffer glows); customEmissiveColorSelector is accepted, unused.
	B->set("GlowLayer", value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		auto o = objptr::make();
		double inten = 1.0;
		if (a.size() > 2 && a[2].is_object()) { inten = num_prop(a[2].as_object(), "intensity", 1.0); }
		o->set("intensity", value{inten});
		o->set("blurKernelSize", value{16.0});
		o->set("customEmissiveColorSelector", value{});
		o->set("__disposed", value{0.0});
		o->set("includedOnlyMeshes", value::array({}));
		o->set("excludedMeshes", value::array({}));
		o->set("dispose", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
			if (const objptr self = self_of(cx)) { self->set("__disposed", value{1.0}); }
			return value{};
		}, "dispose"));
		// include/exclude lists are REAL: they hold mesh __mesh ids and gate which
		// meshes seed the bloom (do_render builds a coverage mask from them).
		const auto edit_list = [](const char * key, bool add) {
			return value::function([key, add](ctjs::context & cx, const std::vector<value> & a) -> value {
				const objptr self = self_of(cx);
				if (!self || a.empty() || !a[0].is_object()) { return value{}; }
				const double mid = num_prop(a[0].as_object(), "__mesh", -1);
				if (mid < 0) { return value{}; }
				value * lst = self->find(key);
				if (lst == nullptr || !lst->is_array()) { return value{}; }
				auto & arr = *lst->as_array(); // std::vector<value>&
				if (add) {
					for (const value & e : arr) { if (e.to_number() == mid) { return value{}; } }
					arr.push_back(value{mid});
				} else {
					for (size_t k = 0; k < arr.size(); ++k) {
						if (arr[k].to_number() == mid) { arr.erase(arr.begin() + static_cast<std::ptrdiff_t>(k)); break; }
					}
				}
				return value{};
			}, key);
		};
		o->set("addIncludedOnlyMesh", edit_list("includedOnlyMeshes", true));
		o->set("removeIncludedOnlyMesh", edit_list("includedOnlyMeshes", false));
		o->set("addExcludedMesh", edit_list("excludedMeshes", true));
		o->set("removeExcludedMesh", edit_list("excludedMeshes", false));
		o->set("referenceMeshToUseItsOwnMaterial", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "referenceMeshToUseItsOwnMaterial"));
		if (a.size() > 1 && a[1].is_object()) {
			const int si = index_of(a[1].as_object(), "__scene");
			if (si >= 0 && si < static_cast<int>(W->scenes.size())) {
				W->scenes[static_cast<size_t>(si)].glow_layers.push_back(o);
			}
		}
		return value{o};
	}, "GlowLayer"));
	// --- ActionManager / ExecuteCodeAction: REAL for OnEveryFrameTrigger (fired
	// each frame by scene.render). Other triggers (pick/pointer/key) are stored on
	// the manager but not yet dispatched - ctbrowser routes input through the DOM,
	// which is what the games actually use.
	value ActionManager = value::function([W](ctjs::context &, const std::vector<value> & a) -> value {
		auto o = objptr::make();
		o->set("__actions", value::array({}));
		o->set("registerAction", value::function([](ctjs::context & cx, const std::vector<value> & args) -> value {
			const objptr self = self_of(cx);
			if (self && !args.empty()) {
				if (const value * acts = self->find("__actions"); acts != nullptr && acts->is_array()) {
					acts->as_array()->push_back(args[0]);
				}
			}
			return args.empty() ? value{} : args[0]; // Babylon returns the action
		}, "registerAction"));
		o->set("dispose", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
			if (const objptr self = self_of(cx)) { self->set("__actions", value::array({})); }
			return value{};
		}, "dispose"));
		o->set("hasSpecificTrigger", value::function([](ctjs::context &, const std::vector<value> &) { return value{false}; }, "hasSpecificTrigger"));
		// register with the scene (arg 0) so OnEveryFrameTrigger actions fire per frame
		if (!a.empty() && a[0].is_object()) {
			const int si = index_of(a[0].as_object(), "__scene");
			if (si >= 0 && si < static_cast<int>(W->scenes.size())) {
				W->scenes[static_cast<size_t>(si)].action_managers.push_back(o);
			}
		}
		return value{o};
	}, "ActionManager");
	// trigger-id statics (Babylon's enum) so `ActionManager.OnEveryFrameTrigger` resolves
	for (const auto & [nm, id] : std::initializer_list<std::pair<const char *, double>>{
	         {"NothingTrigger", 0}, {"OnPickTrigger", 1}, {"OnLeftPickTrigger", 2},
	         {"OnRightPickTrigger", 3}, {"OnCenterPickTrigger", 4}, {"OnPickDownTrigger", 5},
	         {"OnDoublePickTrigger", 6}, {"OnPickUpTrigger", 7}, {"OnLongPressTrigger", 8},
	         {"OnPointerOverTrigger", 9}, {"OnPointerOutTrigger", 10}, {"OnEveryFrameTrigger", 11},
	         {"OnIntersectionEnterTrigger", 12}, {"OnIntersectionExitTrigger", 13},
	         {"OnKeyDownTrigger", 14}, {"OnKeyUpTrigger", 15}, {"OnPickOutTrigger", 16}}) {
		set_static(ActionManager, nm, value{id});
	}
	B->set("ActionManager", ActionManager);
	B->set("ExecuteCodeAction", value::function([](ctjs::context &, const std::vector<value> & a) -> value {
		// ExecuteCodeAction(triggerOptions, func[, condition]): triggerOptions is a
		// trigger id (number) or { trigger, parameter }
		auto o = objptr::make();
		double trig = 0;
		if (!a.empty()) {
			if (a[0].is_object()) { trig = num_prop(a[0].as_object(), "trigger", 0); }
			else { trig = a[0].to_number(); }
		}
		o->set("__trigger", value{trig});
		o->set("trigger", value{trig});
		if (a.size() > 1 && a[1].is_function()) { o->set("__func", a[1]); }
		return value{o};
	}, "ExecuteCodeAction"));

	// --- BABYLON.GUI (the bundler maps @babylonjs/gui -> BABYLON.GUI)
	{
		auto GUI = objptr::make();
		// AdvancedDynamicTexture.CreateFullscreenUI -> a texture that collects
		// controls (addControl); rendering the HUD is a later step
		{
			value ADT = value::function([](ctjs::context &, const std::vector<value> &) -> value {
				return value{objptr::make()};
			}, "AdvancedDynamicTexture");
			set_static(ADT, "CreateFullscreenUI", value::function([W](ctjs::context &, const std::vector<value> &) -> value {
				auto o = objptr::make();
				o->set("controls", value::array({}));
				o->set("addControl", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
					objptr self = self_of(cx);
					if (self && !a.empty()) {
						const value * c = self->find("controls");
						if (c != nullptr && c->is_array()) { c->as_array()->push_back(a[0]); }
					}
					return cx.current_this;
				}, "addControl"));
				object_t cv;
				const double cw = W->target != nullptr ? W->target->canvas_w : 1280;
				const double chh = W->target != nullptr ? W->target->canvas_h : 720;
				cv.set("width", value{cw});
				cv.set("height", value{chh});
				o->set("_canvas", value::object(std::move(cv)));
				o->set("removeControl", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
					objptr self = self_of(cx);
					if (self && !a.empty()) {
						if (const value * c = self->find("controls"); c != nullptr && c->is_array()) {
							auto & arr = *c->as_array();
							for (std::size_t k = 0; k < arr.size(); ++k) {
								if (arr[k].is_object() && a[0].is_object() && arr[k].as_object() == a[0].as_object()) {
									arr.erase(arr.begin() + static_cast<std::ptrdiff_t>(k));
									break;
								}
							}
						}
					}
					return value{};
				}, "removeControl"));
				o->set("dispose", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "dispose"));
				W->guis.push_back(o); // rendered as a 2D overlay by render_guis
				return value{o};
			}, "CreateFullscreenUI"));
			GUI->set("AdvancedDynamicTexture", ADT);
		}
		// TextBlock: data props the game sets (text/color/font/alignment/pos)
		GUI->set("TextBlock", value::function([](ctjs::context &, const std::vector<value> &) -> value {
			auto o = objptr::make();
			o->set("text", value{std::string{}});
			o->set("color", value{std::string{"white"}});
			o->set("fontFamily", value{std::string{}});
			o->set("fontSize", value{18.0});
			o->set("left", value{0.0});
			o->set("top", value{0.0});
			o->set("textVerticalAlignment", value{0.0});
			o->set("textHorizontalAlignment", value{0.0});
			o->set("isVisible", value{true});
			for (const char * nm : {"dispose", "addControl"}) {
				o->set(nm, value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, nm));
			}
			return value{o};
		}, "TextBlock"));
		// Control: alignment constants (BabylonJS values)
		{
			value Control = value::function([](ctjs::context &, const std::vector<value> &) -> value {
				return value{objptr::make()};
			}, "Control");
			set_static(Control, "VERTICAL_ALIGNMENT_TOP", value{0.0});
			set_static(Control, "VERTICAL_ALIGNMENT_BOTTOM", value{1.0});
			set_static(Control, "VERTICAL_ALIGNMENT_CENTER", value{2.0});
			set_static(Control, "HORIZONTAL_ALIGNMENT_LEFT", value{0.0});
			set_static(Control, "HORIZONTAL_ALIGNMENT_RIGHT", value{1.0});
			set_static(Control, "HORIZONTAL_ALIGNMENT_CENTER", value{2.0});
			GUI->set("Control", Control);
		}
		// mobile-only / unused controls: constructible stubs
		for (const char * nm : {"Rectangle", "Ellipse", "Line", "Style", "Image", "Button", "StackPanel"}) {
			GUI->set(nm, value::function([](ctjs::context &, const std::vector<value> &) -> value {
				auto o = objptr::make();
				o->set("addControl", value::function([](ctjs::context & cx, const std::vector<value> &) { return cx.current_this; }, "addControl"));
				return value{o};
			}, nm));
		}
		B->set("GUI", value{GUI});
	}

	// --- statics on Engine / Scene
	if (value * eng = B->find("Engine")) {
		object_t ae;
		ae.set("unlock", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "unlock"));
		ae.set("audioContext", value{});
		ae.set("canUseWebAudio", value{false});
		set_static(*eng, "audioEngine", value::object(std::move(ae)));
	}
	if (value * scn = B->find("Scene")) {
		set_static(*scn, "FOGMODE_NONE", value{0.0});
		set_static(*scn, "FOGMODE_EXP", value{1.0});
		set_static(*scn, "FOGMODE_EXP2", value{2.0});
		set_static(*scn, "FOGMODE_LINEAR", value{3.0});
	}

	return value{B};
}

} // namespace detail

// Install the BABYLON global (a fresh software-rendered 3D world) into
// the engine's binding set. Called from engine::all_bindings, so it is
// present for the initial script run and after location.reload().
inline void install(std::vector<ctjs::binding> & out, dom_events & ev, image_store & images) {
	auto W = std::make_shared<detail::world>();
	out.push_back({"BABYLON", detail::build_babylon(W, ev, images)});
}

#endif // CTBROWSER_BABYLON_RENDER_ONLY

} // namespace ctbrowser::babylon

#endif
