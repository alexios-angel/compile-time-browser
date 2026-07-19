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
// flat-shaded, z-buffered, perspective 3D and basic ArcRotate mouse
// orbit. INTENTIONALLY OUT OF SCOPE: textures, PBR, glTF/GLB loading,
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
#endif

#ifndef CTBROWSER_IN_A_MODULE
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <boost/qvm/vec.hpp>
#include <boost/qvm/vec_access.hpp>
#include <boost/qvm/mat.hpp>
#include <bit>
#include <boost/math/ccmath/sqrt.hpp>
#include <boost/math/ccmath/floor.hpp>
#include <boost/math/ccmath/ceil.hpp>
#include <boost/math/ccmath/fabs.hpp>
#endif

namespace ctbrowser::babylon {

// ============================================================monospace
//  r3d — the software 3D renderer (Boost.QVM math + z-buffered raster)
//  Pure C++: no ctjs, no DOM. Column-vector convention (clip = M * p),
//  LEFT-HANDED (Babylon default): +X right, +Y up, +Z into the screen.
// ============================================================

namespace r3d {

using vec3 = boost::qvm::vec<double, 3>;
using vec4 = boost::qvm::vec<double, 4>;
using mat4 = boost::qvm::mat<double, 4, 4>;

namespace bmc = boost::math::ccmath; // constexpr sqrt/floor/ceil/fabs

constexpr vec3 V3(double x, double y, double z) noexcept {
	vec3 v;
	v.a[0] = x; v.a[1] = y; v.a[2] = z;
	return v;
}

// --- vector helpers (hand-rolled so they fold at compile time; the QVM
// operator overloads aren't constexpr, so we don't use them)
constexpr vec3 sub(const vec3 & a, const vec3 & b) noexcept {
	return V3(a.a[0] - b.a[0], a.a[1] - b.a[1], a.a[2] - b.a[2]);
}
constexpr double dot3(const vec3 & a, const vec3 & b) noexcept {
	return a.a[0] * b.a[0] + a.a[1] * b.a[1] + a.a[2] * b.a[2];
}
constexpr vec3 cross3(const vec3 & a, const vec3 & b) noexcept {
	return V3(a.a[1] * b.a[2] - a.a[2] * b.a[1], a.a[2] * b.a[0] - a.a[0] * b.a[2],
	          a.a[0] * b.a[1] - a.a[1] * b.a[0]);
}
constexpr double mag3(const vec3 & a) noexcept { return bmc::sqrt(dot3(a, a)); }
constexpr vec3 norm3(const vec3 & a) noexcept {
	const double m = mag3(a);
	return m > 1e-12 ? V3(a.a[0] / m, a.a[1] / m, a.a[2] / m) : V3(0, 0, 0);
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
constexpr double fcos(double rad) noexcept { double s = 0, c = 0; fsincos(rad, s, c); return c; }
constexpr double fsin(double rad) noexcept { double s = 0, c = 0; fsincos(rad, s, c); return s; }
constexpr double ftan(double rad) noexcept {
	double s = 0, c = 0;
	fsincos(rad, s, c);
	return c != 0.0 ? s / c : 0.0;
}

// --- matrix builders (explicit element fills, column-vector M*p)
constexpr mat4 identity() noexcept {
	mat4 m;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c) m.a[r][c] = (r == c) ? 1.0 : 0.0;
	return m;
}
constexpr mat4 translation(double x, double y, double z) noexcept {
	mat4 m = identity();
	m.a[0][3] = x; m.a[1][3] = y; m.a[2][3] = z;
	return m;
}
constexpr mat4 scaling(double x, double y, double z) noexcept {
	mat4 m = identity();
	m.a[0][0] = x; m.a[1][1] = y; m.a[2][2] = z;
	return m;
}
constexpr mat4 rotationX(double t) noexcept {
	mat4 m = identity();
	const double c = fcos(t), s = fsin(t);
	m.a[1][1] = c; m.a[1][2] = -s; m.a[2][1] = s; m.a[2][2] = c;
	return m;
}
constexpr mat4 rotationY(double t) noexcept {
	mat4 m = identity();
	const double c = fcos(t), s = fsin(t);
	m.a[0][0] = c; m.a[0][2] = s; m.a[2][0] = -s; m.a[2][2] = c;
	return m;
}
constexpr mat4 rotationZ(double t) noexcept {
	mat4 m = identity();
	const double c = fcos(t), s = fsin(t);
	m.a[0][0] = c; m.a[0][1] = -s; m.a[1][0] = s; m.a[1][1] = c;
	return m;
}
// 4x4 matrix multiply (hand-rolled; QVM's operator* is not constexpr)
constexpr mat4 matmul(const mat4 & a, const mat4 & b) noexcept {
	mat4 m;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c) {
			double sum = 0.0;
			for (int k = 0; k < 4; ++k) { sum += a.a[r][k] * b.a[k][c]; }
			m.a[r][c] = sum;
		}
	return m;
}
// Babylon mesh.rotation Vector3 (rx,ry,rz) = yaw-pitch-roll, applied YXZ
constexpr mat4 rotationYPR(double rx, double ry, double rz) noexcept {
	return matmul(rotationY(ry), matmul(rotationX(rx), rotationZ(rz)));
}
constexpr mat4 mul(const mat4 & a, const mat4 & b) noexcept { return matmul(a, b); }

// left-handed perspective (column-vector: clip = P * viewPos)
constexpr mat4 perspectiveFovLH(double fov, double aspect, double zn, double zf) noexcept {
	mat4 m;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c) m.a[r][c] = 0.0;
	const double f = 1.0 / ftan(fov * 0.5);
	m.a[0][0] = f / aspect;
	m.a[1][1] = f;
	m.a[2][2] = zf / (zf - zn);
	m.a[2][3] = -zn * zf / (zf - zn);
	m.a[3][2] = 1.0; // w = z (LH)
	return m;
}
// left-handed lookAt (column-vector view matrix)
constexpr mat4 lookAtLH(const vec3 & eye, const vec3 & target, const vec3 & up) noexcept {
	const vec3 z = norm3(sub(target, eye));       // forward, +Z into screen
	const vec3 x = norm3(cross3(up, z));          // right
	const vec3 y = cross3(z, x);                   // true up
	mat4 m = identity();
	m.a[0][0] = x.a[0]; m.a[0][1] = x.a[1]; m.a[0][2] = x.a[2]; m.a[0][3] = -dot3(x, eye);
	m.a[1][0] = y.a[0]; m.a[1][1] = y.a[1]; m.a[1][2] = y.a[2]; m.a[1][3] = -dot3(y, eye);
	m.a[2][0] = z.a[0]; m.a[2][1] = z.a[1]; m.a[2][2] = z.a[2]; m.a[2][3] = -dot3(z, eye);
	m.a[3][0] = 0; m.a[3][1] = 0; m.a[3][2] = 0; m.a[3][3] = 1;
	return m;
}

// transform a point (w=1); returns the full vec4 (keep w for the divide)
constexpr vec4 xform(const mat4 & m, const vec3 & p) noexcept {
	vec4 v;
	const double px = p.a[0], py = p.a[1], pz = p.a[2];
	for (int r = 0; r < 4; ++r)
		v.a[r] = m.a[r][0]*px + m.a[r][1]*py + m.a[r][2]*pz + m.a[r][3];
	return v;
}

// --- geometry
struct geo {
	std::vector<vec3> verts;
	std::vector<std::array<int, 3>> tris;
};

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
		const double phi = M_PI * (double(i) / rings);       // 0..pi
		for (int j = 0; j <= sectors; ++j) {
			const double theta = 2.0 * M_PI * (double(j) / sectors);
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
		const double a = 2.0 * M_PI * (double(i) / n);
		g.verts.push_back(V3(r * fcos(a), hh, r * fsin(a)));
	}
	for (int i = 0; i < n; ++i) {
		const double a = 2.0 * M_PI * (double(i) / n);
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
				lit += L.intensity * std::max(0.0, dot3(N, V3(-d.a[0], -d.a[1], -d.a[2])));
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
		if (c0.a[3] <= eps || c1.a[3] <= eps || c2.a[3] <= eps) { return; } // near-plane guard

		// screen coords + depth
		const double W = w, H = h;
		auto scr = [&](const vec4 & c, double & sx, double & sy, double & sz) {
			const double iw = 1.0 / c.a[3];
			sx = (c.a[0] * iw * 0.5 + 0.5) * W;
			sy = (1.0 - (c.a[1] * iw * 0.5 + 0.5)) * H;
			sz = c.a[2] * iw;
		};
		double x0, y0, z0, x1, y1, z1, x2, y2, z2;
		scr(c0, x0, y0, z0); scr(c1, x1, y1, z1); scr(c2, x2, y2, z2);

		const double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
		if (bmc::fabs(area) < 1e-9) { return; }             // degenerate
		if (it.cull && area <= 0) { return; }                // backface (CW in screen space)

		const vec3 N = norm3(cross3(sub(V3(w1.a[0], w1.a[1], w1.a[2]), V3(w0.a[0], w0.a[1], w0.a[2])),
		                            sub(V3(w2.a[0], w2.a[1], w2.a[2]), V3(w0.a[0], w0.a[1], w0.a[2]))));
		// front faces have the normal facing roughly toward the camera for
		// our winding; flip so lighting uses the visible side
		const double lit = shade(area > 0 ? N : V3(-N.a[0], -N.a[1], -N.a[2]), lights);
		const uint32_t color = pack(it.diffuse, lit);

		int minx = static_cast<int>(bmc::floor(std::min({x0, x1, x2})));
		int maxx = static_cast<int>(bmc::ceil(std::max({x0, x1, x2})));
		int miny = static_cast<int>(bmc::floor(std::min({y0, y1, y2})));
		int maxy = static_cast<int>(bmc::ceil(std::max({y0, y1, y2})));
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
				if (depth < zbuf_[idx]) {
					zbuf_[idx] = depth;
					px[idx] = color;
				}
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
struct material { std::string name; r3d::rgba base{0.8, 0.8, 0.8, 1}; };
struct primitive {
	std::vector<r3d::vec3> verts;
	std::vector<std::array<int, 3>> tris;
	int material = -1;
	std::string node_name;
};
struct model {
	std::vector<primitive> prims;
	std::vector<material> materials;
	r3d::vec3 bmin{}, bmax{};
	bool ok = false;
};

// node "matrix" is column-major (glTF spec) -> our a[row][col]
constexpr r3d::mat4 node_matrix(const jval & node) {
	if (const jval * m = node.get("matrix")) {
		if (m->size() == 16) {
			r3d::mat4 M = r3d::identity();
			for (int c = 0; c < 4; ++c)
				for (int r = 0; r < 4; ++r) M.a[r][c] = (*m)[static_cast<size_t>(c * 4 + r)].as_num();
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
		R.a[0][0] = 1 - 2 * (y * y + z * z); R.a[0][1] = 2 * (x * y - z * w); R.a[0][2] = 2 * (x * z + y * w);
		R.a[1][0] = 2 * (x * y + z * w); R.a[1][1] = 1 - 2 * (x * x + z * z); R.a[1][2] = 2 * (y * z - x * w);
		R.a[2][0] = 2 * (x * z - y * w); R.a[2][1] = 2 * (y * z + x * w); R.a[2][2] = 1 - 2 * (x * x + y * y);
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
			}
			out.materials.push_back(mm);
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
				const r3d::vec3 v = r3d::V3(w.a[0], w.a[1], w.a[2]);
				out_p.verts.push_back(v);
				if (first) { out.bmin = out.bmax = v; first = false; }
				for (int c = 0; c < 3; ++c) {
					out.bmin.a[c] = std::min(out.bmin.a[c], v.a[c]);
					out.bmax.a[c] = std::max(out.bmax.a[c], v.a[c]);
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
				out_p.tris.push_back({idx[i], idx[i + 1], idx[i + 2]});
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
	return r3d::V3(num_prop(o, "x", dflt.a[0]), num_prop(o, "y", dflt.a[1]),
	               num_prop(o, "z", dflt.a[2]));
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

// --- world state
struct mesh_rec {
	r3d::geo geom;
	objptr handle;
	bool cull = true;
	bool disposed = false;
	bool enabled = true;
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
	bool loop_active = false;
	double prev_ms = 0, last_dt_ms = 16.6;
	// ArcRotate mouse-orbit state (one active camera at a time)
	bool cam_dragging = false;
	double cam_lastx = 0, cam_lasty = 0;
};
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
		return make_vector3(v.a[0], v.a[1], v.a[2]);
	}, "clone"));
	o->set("add", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const r3d::vec3 s = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		const r3d::vec3 t = read_vec3(arg_obj(a, 0), r3d::V3(0, 0, 0));
		return make_vector3(s.a[0] + t.a[0], s.a[1] + t.a[1], s.a[2] + t.a[2]);
	}, "add"));
	o->set("subtract", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const r3d::vec3 s = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		const r3d::vec3 t = read_vec3(arg_obj(a, 0), r3d::V3(0, 0, 0));
		return make_vector3(s.a[0] - t.a[0], s.a[1] - t.a[1], s.a[2] - t.a[2]);
	}, "subtract"));
	o->set("scale", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		const r3d::vec3 s = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		const double k = arg_num(a, 0, 1);
		return make_vector3(s.a[0] * k, s.a[1] * k, s.a[2] * k);
	}, "scale"));
	o->set("length", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
		const r3d::vec3 s = read_vec3(self_of(cx), r3d::V3(0, 0, 0));
		return value{std::sqrt(s.a[0]*s.a[0] + s.a[1]*s.a[1] + s.a[2]*s.a[2])};
	}, "length"));
	o->set("normalize", value::function([](ctjs::context & cx, const std::vector<value> &) -> value {
		const r3d::vec3 n = r3d::norm3(read_vec3(self_of(cx), r3d::V3(0, 0, 0)));
		objptr s = self_of(cx);
		if (s) { s->set("x", value{n.a[0]}); s->set("y", value{n.a[1]}); s->set("z", value{n.a[2]}); }
		return cx.current_this;
	}, "normalize"));
	return value{o};
}

inline value make_color3(double r, double g, double b) {
	auto o = objptr::make();
	o->set("r", value{r}); o->set("g", value{g}); o->set("b", value{b});
	return value{o};
}
inline value make_color4(double r, double g, double b, double a) {
	auto o = objptr::make();
	o->set("r", value{r}); o->set("g", value{g}); o->set("b", value{b}); o->set("a", value{a});
	return value{o};
}

// attach statics to a function value (rides on function_t::props)
inline void set_static(value & fn, const char * name, value v) {
	if (!fn.is_function()) { return; }
	if (!fn.as_function()->props) { fn.as_function()->props = objptr::make(); }
	fn.as_function()->props->set(name, std::move(v));
}

// --- the core render: read the scene's meshes/lights/camera back from
//     their live JS handles and rasterize into the target canvas pixels
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
			eye = r3d::V3(target.a[0] + radius * r3d::fcos(alpha) * r3d::fsin(beta),
			              target.a[1] + radius * r3d::fcos(beta),
			              target.a[2] + radius * r3d::fsin(alpha) * r3d::fsin(beta));
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
		const r3d::vec3 p = read_vec3(child_obj(M.handle, "position"), r3d::V3(0, 0, 0));
		const r3d::vec3 rot = read_vec3(child_obj(M.handle, "rotation"), r3d::V3(0, 0, 0));
		const r3d::vec3 s = read_vec3(child_obj(M.handle, "scaling"), r3d::V3(1, 1, 1));
		r3d::draw_item it;
		it.g = &M.geom;
		it.world = r3d::matmul(r3d::translation(p.a[0], p.a[1], p.a[2]),
		                       r3d::matmul(r3d::rotationYPR(rot.a[0], rot.a[1], rot.a[2]),
		                                   r3d::scaling(s.a[0], s.a[1], s.a[2])));
		const objptr mat = child_obj(M.handle, "material");
		// StandardMaterial uses diffuseColor; glTF/OpenPBR/PBR use
		// baseColor/albedoColor - honor whichever the material carries
		r3d::rgba mc{0.85, 0.85, 0.85, 1};
		if (mat) {
			if (child_obj(mat, "baseColor")) { mc = read_color(child_obj(mat, "baseColor"), mc); }
			else if (child_obj(mat, "albedoColor")) { mc = read_color(child_obj(mat, "albedoColor"), mc); }
			else { mc = read_color(child_obj(mat, "diffuseColor"), mc); }
		}
		it.diffuse = mc;
		it.cull = M.cull;
		items.push_back(it);
	}
	W->rdr.render(n->pixels.data(), w, h, vw, items, lights);
}

// --- register a mesh/light with its scene (by the scene handle arg)
inline void register_with_scene(const worldptr & W, const objptr & scene, int id, bool is_mesh) {
	const int si = index_of(scene, "__scene");
	if (si < 0 || si >= static_cast<int>(W->scenes.size())) { return; }
	(is_mesh ? W->scenes[static_cast<size_t>(si)].mesh_ids
	         : W->scenes[static_cast<size_t>(si)].light_ids).push_back(id);
}

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
	h->set("dispose", value::function([W, id](ctjs::context &, const std::vector<value> &) -> value {
		W->meshes[static_cast<size_t>(id)].disposed = true;
		return value{};
	}, "dispose"));
	h->set("setEnabled", value::function([W, id](ctjs::context &, const std::vector<value> & a) -> value {
		W->meshes[static_cast<size_t>(id)].enabled = a.empty() || a[0].truthy();
		return value{};
	}, "setEnabled"));
	h->set("getScene", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "getScene"));
	W->meshes.push_back(mesh_rec{std::move(g), h, cull, false, true});
	register_with_scene(W, scene, id, true);
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
	for (const gltf::primitive & p : mdl.prims) {
		const int id = static_cast<int>(W->meshes.size());
		objptr h = objptr::make();
		h->set("name", value{p.node_name});
		h->set("id", value{p.node_name});
		h->set("__mesh", value{static_cast<double>(id)});
		h->set("position", make_vector3(0, 0, 0));
		h->set("rotation", make_vector3(0, 0, 0));
		h->set("scaling", make_vector3(1, 1, 1));
		if (p.material >= 0 && p.material < static_cast<int>(mat_handles.size())) {
			h->set("material", value{mat_handles[static_cast<size_t>(p.material)]});
		} else {
			h->set("material", value{});
		}
		r3d::geo g{p.verts, p.tris};
		W->meshes.push_back(mesh_rec{std::move(g), h, true, false, true});
		sc.mesh_ids.push_back(id);
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
	h->set("direction", make_vector3(dir.a[0], dir.a[1], dir.a[2]));
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
		beta = beta < 0.05 ? 0.05 : (beta > M_PI - 0.05 ? M_PI - 0.05 : beta);
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
	h->set("__camera", value{static_cast<double>(id)});
	h->set("setTarget", value::function([](ctjs::context & cx, const std::vector<value> & a) -> value {
		objptr s = self_of(cx);
		if (s && !a.empty() && a[0].is_object()) { s->set("target", a[0]); }
		return value{};
	}, "setTarget"));
	h->set("attachControl", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "attachControl"));
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
		do_render(W, id);
		return value{};
	}, "render"));
	// commonly-probed no-ops so real scripts don't throw
	for (const char * nm : {"registerBeforeRender", "registerAfterRender", "onBeforeRenderObservable",
	                        "beforeRender", "dispose"}) {
		h->set(nm, value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, nm));
	}
	h->set("getEngine", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "getEngine"));

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
			c = r3d::V3((sc.bmin.a[0] + sc.bmax.a[0]) * 0.5, (sc.bmin.a[1] + sc.bmax.a[1]) * 0.5,
			            (sc.bmin.a[2] + sc.bmax.a[2]) * 0.5);
			rad = 0.001;
			for (int k = 0; k < 3; ++k) { rad = std::max(rad, sc.bmax.a[k] - sc.bmin.a[k]); }
		}
		const objptr target = make_vector3(c.a[0], c.a[1], c.a[2]).as_object();
		return make_camera_arc(W, ev, "default_camera", -M_PI / 2, M_PI / 2.5, rad * 2.2, target, sc.handle);
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
	h->set("dispose", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "dispose"));
	h->set("displayLoadingUI", value::function([](ctjs::context &, const std::vector<value> &) { return value{}; }, "displayLoadingUI"));
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
