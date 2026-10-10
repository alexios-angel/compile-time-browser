//===- ctbrowser/lib/Style/easing/effects.cpp - Interpolation -------------===//
//
// Interpolation and accumulation of the visual-effect values: transform lists
// (function by function, or through 2D and 3D matrix decomposition), filter
// lists, and the individual `rotate` property. Section numbers without a spec
// name refer to CSS Transforms 2.
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

namespace ctbrowser::style::easing_detail {

// The primitive a transform function belongs to: two functions pair for
// interpolation only when their primitives match.
[[nodiscard]] std::string_view primitive_of(std::string_view name) {
    if (name.starts_with("translate")) { return "translate"; }
    if (name.starts_with("scale")) { return "scale"; }
    // rotate() is rotate3d() about z (CSS Transforms 2 §12): the two pair.
    if (name.starts_with("rotate")) { return "rotate"; }
    return name;
}

// A transform list, or an empty list for `none`, or nullopt for one this does
// not model (a length other than px, a percentage outside a translate), which
// then animates discretely. Every function is read into a canonical form so
// the same primitive always pairs: translateX/Y as translate(), scaleX/Y as
// scale(), rotateZ as rotate(), and the 3D ones as translate3d(), scale3d(),
// rotate3d(), perspective() (holding the reciprocal of its length, 0 for
// `none`, which is what CSS Transforms 2 §15 interpolates) and matrix3d().
[[nodiscard]] std::optional<std::vector<transform_fn>> parse_transforms(std::string_view text) {
    using css::token_type;
    std::vector<transform_fn> out;
    if (ascii_iequals(text, "none")) { return out; }
    const css::token_stream ts = css::tokenize(text);
    std::size_t at = 0;
    for (;;) {
        while (ts.tokens[at].type == token_type::whitespace) { ++at; }
        if (ts.tokens[at].type == token_type::eof) { break; }
        if (ts.tokens[at].type != token_type::function) { return std::nullopt; }
        const std::string_view raw = ts.text_of(ts.tokens[at]);
        transform_fn fn;
        fn.name = ascii_lower_copy(raw.substr(0, raw.size() - 1));
        fn.spelled = fn.name;
        ++at;
        std::vector<bool> is_length;
        bool none = false;
        for (;;) {
            while (ts.tokens[at].type == token_type::whitespace) { ++at; }
            const css::css_token & t = ts.tokens[at];
            if (t.type == token_type::eof) { return std::nullopt; }
            ++at;
            if (t.type == token_type::close_paren) { break; }
            if (t.type == token_type::comma) { continue; }
            fn.pct.push_back(0);
            if (t.type == token_type::ident && fn.name == "perspective" &&
                ascii_iequals(ts.text_of(t), "none")) {
                none = true;
                fn.args.push_back(0);
                is_length.push_back(true);
            } else if (t.type == token_type::number) {
                fn.args.push_back(t.number);
                is_length.push_back(false);
            } else if (t.type == token_type::percentage) {
                fn.args.push_back(0);
                fn.pct.back() = t.number;
                is_length.push_back(true);
            } else if (t.type == token_type::dimension) {
                const std::string unit = ascii_lower_copy(ts.unit_of(t));
                if (unit == "px") {
                    fn.args.push_back(t.number);
                    is_length.push_back(true);
                } else if (unit == "deg") {
                    fn.args.push_back(t.number);
                    is_length.push_back(false);
                } else if (unit == "rad") {
                    fn.args.push_back(t.number * 180.0 / std::numbers::pi);
                    is_length.push_back(false);
                } else if (unit == "grad") {
                    fn.args.push_back(t.number * 0.9);
                    is_length.push_back(false);
                } else if (unit == "turn") {
                    fn.args.push_back(t.number * 360.0);
                    is_length.push_back(false);
                } else {
                    return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
        }
        const std::size_t n = fn.args.size();
        const bool translate = fn.name.starts_with("translate");
        const bool lengths = translate || fn.name == "perspective";
        for (std::size_t i = 0; i < n; ++i) {
            if (is_length[i] != lengths && !(lengths && fn.args[i] == 0.0)) { return std::nullopt; }
            if (!translate && fn.pct[i] != 0) { return std::nullopt; }
        }
        // A translation along z is a length and never a percentage.
        if ((fn.name == "translatez" || fn.name == "translate3d") && n >= 1 && fn.pct[n - 1] != 0) {
            return std::nullopt;
        }
        const auto set = [&fn](std::string name, std::vector<double> args) {
            fn.name = std::move(name);
            fn.args = std::move(args);
        };
        const auto append = [&fn](double v) { fn.args.push_back(v), fn.pct.push_back(0); };
        const auto prepend = [&fn](double v) {
            fn.args.insert(fn.args.begin(), v), fn.pct.insert(fn.pct.begin(), 0);
        };
        if (fn.name == "translatex" && n == 1) {
            fn.name = "translate", append(0);
        } else if (fn.name == "translatey" && n == 1) {
            fn.name = "translate", prepend(0);
        } else if (fn.name == "translate" && n == 1) {
            append(0);
        } else if (fn.name == "translatez" && n == 1) {
            fn.name = "translate3d", prepend(0), prepend(0);
        } else if (fn.name == "scalex" && n == 1) {
            fn.name = "scale", append(1);
        } else if (fn.name == "scaley" && n == 1) {
            fn.name = "scale", prepend(1);
        } else if (fn.name == "scale" && n == 1) {
            append(fn.args[0]);
        } else if (fn.name == "scalez" && n == 1) {
            fn.name = "scale3d", prepend(1), prepend(1);
        } else if (fn.name == "skew" && n == 1) {
            append(0);
        } else if (fn.name == "rotatez" && n == 1) {
            fn.name = "rotate";
        } else if (fn.name == "rotatex" && n == 1) {
            set("rotate3d", {1, 0, 0, fn.args[0]});
        } else if (fn.name == "rotatey" && n == 1) {
            set("rotate3d", {0, 1, 0, fn.args[0]});
        } else if (fn.name == "perspective" && n == 1) {
            // A depth of zero is taken as 1px (CSS Transforms 2 §13.1).
            fn.args[0] = none ? 0.0 : 1.0 / std::max(fn.args[0], 1.0);
        }
        fn.pct.resize(fn.args.size(), 0);
        const std::size_t count = fn.args.size();
        const bool shaped =
            (fn.name == "matrix" && count == 6) || (fn.name == "translate" && count == 2) ||
            (fn.name == "scale" && count == 2) || (fn.name == "rotate" && count == 1) ||
            (fn.name == "skew" && count == 2) || (fn.name == "skewx" && count == 1) ||
            (fn.name == "skewy" && count == 1) || (fn.name == "translate3d" && count == 3) ||
            (fn.name == "scale3d" && count == 3) || (fn.name == "rotate3d" && count == 4) ||
            (fn.name == "perspective" && count == 1) || (fn.name == "matrix3d" && count == 16);
        if (!shaped) { return std::nullopt; }
        out.push_back(std::move(fn));
    }
    return out;
}

// A 2D function as its 3D primitive, for a pair whose other half is 3D.
void promote(transform_fn & fn) {
    if (fn.name == "translate") {
        fn.name = "translate3d";
        fn.args.push_back(0);
        fn.pct.push_back(0);
    } else if (fn.name == "scale") {
        fn.name = "scale3d";
        fn.args.push_back(1);
        fn.pct.push_back(0);
    } else if (fn.name == "rotate") {
        fn.name = "rotate3d";
        fn.args = {0, 0, 1, fn.args[0]};
        fn.pct.assign(4, 0);
    }
}

// True for the canonical 3D functions.
[[nodiscard]] bool is_3d(const transform_fn & fn) {
    return fn.name == "translate3d" || fn.name == "scale3d" || fn.name == "rotate3d" ||
           fn.name == "perspective" || fn.name == "matrix3d";
}

// The product m * n: n is applied first.
[[nodiscard]] matrix2d multiply(const matrix2d & m, const matrix2d & n) {
    return {m[0] * n[0] + m[2] * n[1],        m[1] * n[0] + m[3] * n[1],
            m[0] * n[2] + m[2] * n[3],        m[1] * n[2] + m[3] * n[3],
            m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5]};
}

[[nodiscard]] double radians(double deg) {
    return deg * std::numbers::pi / 180.0;
}

[[nodiscard]] matrix2d matrix_of(const transform_fn & fn) {
    const std::vector<double> & a = fn.args;
    if (fn.name == "matrix") { return {a[0], a[1], a[2], a[3], a[4], a[5]}; }
    if (fn.name == "translate") { return {1, 0, 0, 1, a[0], a[1]}; }
    if (fn.name == "scale") { return {a[0], 0, 0, a[1], 0, 0}; }
    if (fn.name == "rotate") {
        const double r = radians(a[0]);
        return {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0};
    }
    if (fn.name == "skew") {
        return {1, std::tan(radians(a[1])), std::tan(radians(a[0])), 1, 0, 0};
    }
    if (fn.name == "skewx") { return {1, 0, std::tan(radians(a[0])), 1, 0, 0}; }
    return {1, std::tan(radians(a[0])), 0, 1, 0, 0}; // skewY.
}

[[nodiscard]] matrix2d matrix_of(const std::vector<transform_fn> & list) {
    matrix2d m{1, 0, 0, 1, 0, 0};
    for (const transform_fn & fn : list) { m = multiply(m, matrix_of(fn)); }
    return m;
}

// --- 4x4 matrices, column-major as matrix3d() lists them -------------------

[[nodiscard]] matrix3d identity3d() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

[[nodiscard]] matrix3d multiply(const matrix3d & a, const matrix3d & b) {
    matrix3d out{};
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t r = 0; r < 4; ++r) {
            double v = 0;
            for (std::size_t k = 0; k < 4; ++k) { v += a[k * 4 + r] * b[c * 4 + k]; }
            out[c * 4 + r] = v;
        }
    }
    return out;
}

// rotate3d()'s matrix, CSS Transforms 2 §13.2, about the normalised axis.
[[nodiscard]] matrix3d rotation3d(double x, double y, double z, double deg) {
    const double length = std::hypot(x, y, z);
    if (length == 0) { return identity3d(); }
    x /= length, y /= length, z /= length;
    const double half = radians(deg) / 2;
    const double sc = std::sin(half) * std::cos(half);
    const double sq = std::sin(half) * std::sin(half);
    matrix3d m = identity3d();
    m[0] = 1 - 2 * (y * y + z * z) * sq;
    m[1] = 2 * (x * y * sq + z * sc);
    m[2] = 2 * (x * z * sq - y * sc);
    m[4] = 2 * (x * y * sq - z * sc);
    m[5] = 1 - 2 * (x * x + z * z) * sq;
    m[6] = 2 * (y * z * sq + x * sc);
    m[8] = 2 * (x * z * sq + y * sc);
    m[9] = 2 * (y * z * sq - x * sc);
    m[10] = 1 - 2 * (x * x + y * y) * sq;
    return m;
}

[[nodiscard]] matrix3d matrix3d_of(const transform_fn & fn) {
    const std::vector<double> & a = fn.args;
    matrix3d m = identity3d();
    if (fn.name == "translate3d") {
        m[12] = a[0], m[13] = a[1], m[14] = a[2];
    } else if (fn.name == "scale3d") {
        m[0] = a[0], m[5] = a[1], m[10] = a[2];
    } else if (fn.name == "rotate3d") {
        m = rotation3d(a[0], a[1], a[2], a[3]);
    } else if (fn.name == "perspective") {
        m[11] = -a[0];
    } else if (fn.name == "matrix3d") {
        for (std::size_t i = 0; i < 16; ++i) { m[i] = a[i]; }
    } else {
        const matrix2d flat = matrix_of(fn);
        m[0] = flat[0], m[1] = flat[1], m[4] = flat[2], m[5] = flat[3];
        m[12] = flat[4], m[13] = flat[5];
    }
    return m;
}

[[nodiscard]] matrix3d matrix3d_of(const std::vector<transform_fn> & list) {
    matrix3d m = identity3d();
    for (const transform_fn & fn : list) { m = multiply(m, matrix3d_of(fn)); }
    return m;
}

// True when the 4x4 matrix is a 2D affine transform.
[[nodiscard]] bool flat(const matrix3d & m) {
    return m[2] == 0 && m[3] == 0 && m[6] == 0 && m[7] == 0 && m[8] == 0 && m[9] == 0 &&
           m[10] == 1 && m[11] == 0 && m[14] == 0 && m[15] == 1;
}

[[nodiscard]] double determinant3(double a, double b, double c, double d, double e, double f,
                                  double g, double h, double i) {
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

// The inverse by cofactors; nullopt for a singular matrix.
[[nodiscard]] std::optional<matrix3d> inverse(const matrix3d & m) {
    const auto at = [&m](std::size_t r, std::size_t c) { return m[c * 4 + r]; };
    matrix3d out{};
    double det = 0;
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            double minor[9];
            std::size_t k = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                if (i == r) { continue; }
                for (std::size_t j = 0; j < 4; ++j) {
                    if (j != c) { minor[k++] = at(i, j); }
                }
            }
            const double cofactor = ((r + c) % 2 == 0 ? 1 : -1) *
                                    determinant3(minor[0], minor[1], minor[2], minor[3], minor[4],
                                                 minor[5], minor[6], minor[7], minor[8]);
            out[r * 4 + c] = cofactor; // Transposed: the adjugate.
            if (r == 0) { det += at(0, c) * cofactor; }
        }
    }
    if (det == 0) { return std::nullopt; }
    for (double & v : out) { v /= det; }
    return out;
}

// CSS Transforms 2 §16.2.1, decomposing a 3D matrix. `matrix[i][j]` there is
// m[i * 4 + j] here: column i, row j.
[[nodiscard]] std::optional<decomposed3d> decompose3d(matrix3d m) {
    if (m[15] == 0) { return std::nullopt; }
    for (double & v : m) { v /= m[15]; }
    matrix3d perspective_matrix = m;
    perspective_matrix[3] = perspective_matrix[7] = perspective_matrix[11] = 0;
    perspective_matrix[15] = 1;
    const std::optional<matrix3d> inverted = inverse(perspective_matrix);
    if (!inverted) { return std::nullopt; }
    decomposed3d d{};
    if (m[3] != 0 || m[7] != 0 || m[11] != 0) {
        const double rhs[4] = {m[3], m[7], m[11], m[15]};
        for (std::size_t i = 0; i < 4; ++i) {
            d.perspective[i] = 0;
            for (std::size_t j = 0; j < 4; ++j) {
                d.perspective[i] += rhs[j] * (*inverted)[i * 4 + j];
            }
        }
    } else {
        d.perspective = {0, 0, 0, 1};
    }
    d.translate = {m[12], m[13], m[14]};
    std::array<std::array<double, 3>, 3> row{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) { row[i][j] = m[i * 4 + j]; }
    }
    const auto length = [](const std::array<double, 3> & v) {
        return std::hypot(v[0], v[1], v[2]);
    };
    const auto normalise = [&length](std::array<double, 3> & v) {
        const double l = length(v);
        if (l != 0) { v[0] /= l, v[1] /= l, v[2] /= l; }
    };
    const auto dot = [](const std::array<double, 3> & u, const std::array<double, 3> & v) {
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    };
    const auto combine = [](std::array<double, 3> & u, const std::array<double, 3> & v, double k) {
        for (std::size_t i = 0; i < 3; ++i) { u[i] += k * v[i]; }
    };
    d.scale[0] = length(row[0]);
    normalise(row[0]);
    d.skew[0] = dot(row[0], row[1]);
    combine(row[1], row[0], -d.skew[0]);
    d.scale[1] = length(row[1]);
    normalise(row[1]);
    if (d.scale[1] != 0) { d.skew[0] /= d.scale[1]; }
    d.skew[1] = dot(row[0], row[2]);
    combine(row[2], row[0], -d.skew[1]);
    d.skew[2] = dot(row[1], row[2]);
    combine(row[2], row[1], -d.skew[2]);
    d.scale[2] = length(row[2]);
    normalise(row[2]);
    if (d.scale[2] != 0) { d.skew[1] /= d.scale[2], d.skew[2] /= d.scale[2]; }
    const std::array<double, 3> cross{row[1][1] * row[2][2] - row[1][2] * row[2][1],
                                      row[1][2] * row[2][0] - row[1][0] * row[2][2],
                                      row[1][0] * row[2][1] - row[1][1] * row[2][0]};
    if (dot(row[0], cross) < 0) {
        for (std::size_t i = 0; i < 3; ++i) {
            d.scale[i] = -d.scale[i];
            for (double & v : row[i]) { v = -v; }
        }
    }
    d.quaternion[0] = 0.5 * std::sqrt(std::max(1 + row[0][0] - row[1][1] - row[2][2], 0.0));
    d.quaternion[1] = 0.5 * std::sqrt(std::max(1 - row[0][0] + row[1][1] - row[2][2], 0.0));
    d.quaternion[2] = 0.5 * std::sqrt(std::max(1 - row[0][0] - row[1][1] + row[2][2], 0.0));
    d.quaternion[3] = 0.5 * std::sqrt(std::max(1 + row[0][0] + row[1][1] + row[2][2], 0.0));
    if (row[2][1] > row[1][2]) { d.quaternion[0] = -d.quaternion[0]; }
    if (row[0][2] > row[2][0]) { d.quaternion[1] = -d.quaternion[1]; }
    if (row[1][0] > row[0][1]) { d.quaternion[2] = -d.quaternion[2]; }
    return d;
}

// §16.2.2, recomposing - the rotation written as the one the quaternion's
// extraction above reads back, so that recompose(decompose(m)) is m.
[[nodiscard]] matrix3d recompose3d(const decomposed3d & d) {
    matrix3d m = identity3d();
    for (std::size_t i = 0; i < 4; ++i) { m[i * 4 + 3] = d.perspective[i]; }
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 3; ++j) { m[12 + i] += d.translate[j] * m[j * 4 + i]; }
    }
    const double x = d.quaternion[0], y = d.quaternion[1], z = d.quaternion[2], w = d.quaternion[3];
    matrix3d rotation = identity3d();
    rotation[0] = 1 - 2 * (y * y + z * z);
    rotation[1] = 2 * (x * y + z * w);
    rotation[2] = 2 * (x * z - y * w);
    rotation[4] = 2 * (x * y - z * w);
    rotation[5] = 1 - 2 * (x * x + z * z);
    rotation[6] = 2 * (y * z + x * w);
    rotation[8] = 2 * (x * z + y * w);
    rotation[9] = 2 * (y * z - x * w);
    rotation[10] = 1 - 2 * (x * x + y * y);
    m = multiply(m, rotation);
    matrix3d shear = identity3d();
    if (d.skew[2] != 0) {
        shear[9] = d.skew[2];
        m = multiply(m, shear);
    }
    if (d.skew[1] != 0) {
        shear[9] = 0;
        shear[8] = d.skew[1];
        m = multiply(m, shear);
    }
    if (d.skew[0] != 0) {
        shear[8] = 0;
        shear[4] = d.skew[0];
        m = multiply(m, shear);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 4; ++j) { m[i * 4 + j] *= d.scale[i]; }
    }
    return m;
}

// §16.2.3: every part linearly, the rotation by spherical interpolation.
[[nodiscard]] decomposed3d blend3d(const decomposed3d & a, const decomposed3d & b, double p) {
    decomposed3d out{};
    const auto lerp = [p](double x, double y) { return (1 - p) * x + p * y; };
    for (std::size_t i = 0; i < 3; ++i) {
        out.translate[i] = lerp(a.translate[i], b.translate[i]);
        out.scale[i] = lerp(a.scale[i], b.scale[i]);
        out.skew[i] = lerp(a.skew[i], b.skew[i]);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        out.perspective[i] = lerp(a.perspective[i], b.perspective[i]);
    }
    // Take the short way round: q and -q are one rotation, and a half angle
    // over 90 degrees is the long way to it. A 2D pair's rotation goes the
    // short way too (§12.2).
    std::array<double, 4> from = a.quaternion;
    double product = 0;
    for (std::size_t i = 0; i < 4; ++i) { product += from[i] * b.quaternion[i]; }
    if (product < 0) {
        for (double & v : from) { v = -v; }
        product = -product;
    }
    product = std::min(product, 1.0);
    if (product >= 1.0) {
        out.quaternion = from;
        return out;
    }
    const double theta = std::acos(product);
    const double w = std::sin(p * theta) / std::sqrt(1 - product * product);
    for (std::size_t i = 0; i < 4; ++i) {
        out.quaternion[i] = from[i] * (std::cos(p * theta) - product * w) + b.quaternion[i] * w;
    }
    return out;
}

// CSS Transforms 1 §12.2's 2D decomposition: translation, scale, a rotation
// angle in degrees and the residual matrix.
[[nodiscard]] decomposed2d decompose(const matrix2d & m) {
    double row0x = m[0], row0y = m[1], row1x = m[2], row1y = m[3];
    decomposed2d d{m[4], m[5], 0, 0, 0, 1, 0, 0, 1};
    d.sx = std::hypot(row0x, row0y);
    if (d.sx != 0) { row0x /= d.sx, row0y /= d.sx; }
    double skew = row0x * row1x + row0y * row1y;
    row1x -= row0x * skew, row1y -= row0y * skew;
    d.sy = std::hypot(row1x, row1y);
    if (d.sy != 0) { row1x /= d.sy, row1y /= d.sy, skew /= d.sy; }
    if (row0x * row1y - row0y * row1x < 0) {
        d.sx = -d.sx;
        row0x = -row0x, row0y = -row0y;
    }
    d.angle = std::atan2(row0y, row0x) * 180.0 / std::numbers::pi;
    // The residual is the matrix with its rotation taken back out, so that
    // recomposing rotate(angle) * residual gives the rows back.
    const double sn = -row0y, cs = row0x;
    const double m11 = row0x, m12 = row0y, m21 = row1x, m22 = row1y;
    d.m11 = cs * m11 + sn * m21, d.m12 = cs * m12 + sn * m22;
    d.m21 = -sn * m11 + cs * m21, d.m22 = -sn * m12 + cs * m22;
    return d;
}

[[nodiscard]] matrix2d recompose(const decomposed2d & d) {
    matrix2d m{1, 0, 0, 1, d.tx, d.ty};
    const double r = radians(d.angle);
    m = multiply(m, {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0});
    m = multiply(m, {d.m11, d.m12, d.m21, d.m22, 0, 0});
    return multiply(m, {d.sx, 0, 0, d.sy, 0, 0});
}

[[nodiscard]] std::string matrix_text(const matrix2d & m) {
    std::string out{"matrix("};
    for (std::size_t i = 0; i < 6; ++i) {
        if (i != 0) { out += ", "; }
        // Six decimals, which is where the computed serialiser rounds anyway,
        // and no negative zero - a scale through 0 leaves one.
        const double v = std::round(m[i] * 1e6) / 1e6;
        out += css::serialize_number(v == 0 ? 0.0 : v);
    }
    return out + ')';
}

// A 4x4 matrix as the text of the value: `matrix()` when it is 2D.
[[nodiscard]] std::string matrix_text(const matrix3d & m) {
    if (flat(m)) { return matrix_text(matrix2d{m[0], m[1], m[4], m[5], m[12], m[13]}); }
    std::string out{"matrix3d("};
    for (std::size_t i = 0; i < 16; ++i) {
        if (i != 0) { out += ", "; }
        // To nine significant digits: a perspective term is a small number,
        // and six decimals of -1/450 is not the matrix any more.
        std::array<char, 32> buffer{};
        const auto written =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                          std::fabs(m[i]) < 1e-9 ? 0.0 : m[i], std::chars_format::general, 9);
        out.append(buffer.data(), written.ptr);
    }
    return out + ')';
}

// One canonical transform function back to CSS text.
[[nodiscard]] std::string function_text(const transform_fn & fn) {
    if (fn.name == "perspective") {
        if (fn.args[0] <= 0) { return "perspective(none)"; }
        return "perspective(" + css::serialize_number(1 / fn.args[0]) + "px)";
    }
    if (fn.name == "matrix3d") {
        matrix3d m{};
        for (std::size_t i = 0; i < 16; ++i) { m[i] = fn.args[i]; }
        return matrix_text(m);
    }
    std::string out = fn.name + '(';
    const bool translate = fn.name.starts_with("translate");
    const bool angles = fn.name == "rotate" || fn.name.starts_with("skew");
    for (std::size_t i = 0; i < fn.args.size(); ++i) {
        if (i != 0) { out += ", "; }
        if (translate) {
            css::calc_result length;
            length.px = fn.args[i];
            length.percent = fn.pct[i];
            length.has_percent = fn.pct[i] != 0;
            out += css::serialize_calc(length);
            continue;
        }
        out += css::serialize_number(fn.args[i]);
        if (angles || (fn.name == "rotate3d" && i == 3)) { out += "deg"; }
    }
    return out + ')';
}

// The identity function of `fn`'s kind, which pads the shorter list of a pair.
[[nodiscard]] transform_fn identity_like(const transform_fn & fn) {
    transform_fn out{fn.name, {}, {}, fn.spelled};
    if (fn.name == "matrix") {
        out.args = {1, 0, 0, 1, 0, 0};
    } else if (fn.name == "matrix3d") {
        const matrix3d one = identity3d();
        out.args.assign(one.begin(), one.end());
    } else if (fn.name == "scale" || fn.name == "scale3d") {
        out.args.assign(fn.args.size(), 1.0);
    } else if (fn.name == "rotate3d") {
        out.args = {fn.args[0], fn.args[1], fn.args[2], 0}; // About the same axis.
    } else {
        out.args.assign(fn.args.size(), 0.0);
    }
    out.pct.assign(out.args.size(), 0.0);
    return out;
}

// Two single functions blended as matrices - rotate3d() about two different
// axes (§15.1) - or nullopt when either will not decompose.
[[nodiscard]] std::optional<std::string> blend_as_matrices(const std::vector<transform_fn> & a,
                                                           const std::vector<transform_fn> & b,
                                                           double p) {
    const matrix3d x = matrix3d_of(a), y = matrix3d_of(b);
    if (flat(x) && flat(y)) { return std::nullopt; } // The caller's 2D path.
    const std::optional<decomposed3d> u = decompose3d(x);
    const std::optional<decomposed3d> v = decompose3d(y);
    if (!u || !v) { return std::nullopt; }
    return matrix_text(recompose3d(blend3d(*u, *v, p)));
}

// Two lists blended as matrices (§15-16), decomposed as 4x4 matrices - the
// decomposition that keeps a 2D pair's skew too: `matrix(1, 0, 0, 7, 0, 0)`
// to `matrix(7, 0, 1, 1, 0, 0)` passes through `matrix(4, 0, 2, 4, 0, 0)` in
// every engine (WPT transform-interpolation-005). nullopt for a percentage, which
// has no matrix until the box exists, and for a singular matrix.
[[nodiscard]] std::optional<std::string> blend_lists(const std::vector<transform_fn> & a,
                                                     const std::vector<transform_fn> & b,
                                                     double p) {
    for (const std::vector<transform_fn> * list : {&a, &b}) {
        for (const transform_fn & fn : *list) {
            if (std::ranges::any_of(fn.pct, [](double v) { return v != 0; })) {
                return std::nullopt;
            }
        }
    }
    const matrix3d x = matrix3d_of(a), y = matrix3d_of(b);
    const std::optional<decomposed3d> u = decompose3d(x);
    const std::optional<decomposed3d> v = decompose3d(y);
    if (!u || !v) { return std::nullopt; }
    return matrix_text(recompose3d(blend3d(*u, *v, p)));
}

// §15-16: function by function over the lists' longest common prefix of
// functions that pair - a list that has ended pairing as the identity of the
// other's function, which is how `none` and a shorter list are padded - and
// whatever follows it, on both sides, blended as matrices. So
// `rotate(0deg) translate(100px)` to `rotate(720deg) scale(2) translate(200px)`
// turns the rotation through 720 degrees and blends only the rest
// (WPT list-interpolation). A matrix() pair is a pair of matrices, blended so.
[[nodiscard]] std::optional<std::string> interpolate_transform(std::string_view from,
                                                               std::string_view to, double p,
                                                               bool * singular) {
    std::optional<std::vector<transform_fn>> a = parse_transforms(from);
    std::optional<std::vector<transform_fn>> b = parse_transforms(to);
    if (!a || !b) { return std::nullopt; }
    if (a->empty() && b->empty()) { return "none"; }
    // The endpoints are themselves, as written: `translateY(90%)` at 1 is
    // `translateY(90%)` and not its two-argument spelling.
    if (p == 0 && !a->empty()) { return std::string{from}; }
    if (p == 1 && !b->empty()) { return std::string{to}; }
    const auto lerp = [p](double x, double y) { return (1 - p) * x + p * y; };
    // Matrices that will not blend: a percentage is the caller's, and a
    // singular matrix makes the whole pair discrete (CSS Transforms 1 §10) -
    // no transition starts, and an animation flips the lists whole
    // (WPT transform-matrix-composition and
    // transform-non-invertible-no-transition).
    const auto unblended = [&](const std::vector<transform_fn> & x,
                               const std::vector<transform_fn> & y) -> std::optional<std::string> {
        const auto percent = [](const transform_fn & fn) {
            return std::ranges::any_of(fn.pct, [](double v) { return v != 0; });
        };
        if (singular != nullptr && !std::ranges::any_of(x, percent) &&
            !std::ranges::any_of(y, percent)) {
            *singular = true;
        }
        return std::nullopt;
    };
    const std::size_t shorter = std::min(a->size(), b->size());
    std::size_t prefix = 0;
    while (prefix < shorter && primitive_of((*a)[prefix].name) == primitive_of((*b)[prefix].name)) {
        ++prefix;
    }
    if (prefix == shorter) {
        for (std::size_t i = a->size(); i < b->size(); ++i) {
            a->push_back(identity_like((*b)[i]));
        }
        for (std::size_t i = b->size(); i < a->size(); ++i) {
            b->push_back(identity_like((*a)[i]));
        }
        prefix = a->size();
    }
    std::string out;
    for (std::size_t i = 0; i < prefix; ++i) {
        transform_fn x = (*a)[i];
        transform_fn y = (*b)[i];
        if (i != 0) { out += ' '; }
        if (x.name == "matrix" || x.name == "matrix3d") {
            const std::optional<std::string> m = blend_lists({x}, {y}, p);
            if (!m) { return unblended({x}, {y}); }
            out += *m;
            continue;
        }
        if (is_3d(x) != is_3d(y)) {
            promote(x);
            promote(y);
        }
        if (x.name == "rotate3d") {
            // One axis for both - the other's when an angle is zero - and
            // the angle interpolates; two axes are blended as matrices.
            const auto unit = [](transform_fn & r) {
                const double n = std::hypot(r.args[0], r.args[1], r.args[2]);
                if (n == 0) { return false; }
                for (std::size_t k = 0; k < 3; ++k) { r.args[k] /= n; }
                return true;
            };
            const bool ux = unit(x), uy = unit(y);
            if (!ux || x.args[3] == 0) {
                std::copy_n(y.args.begin(), 3, x.args.begin());
            } else if (!uy || y.args[3] == 0) {
                std::copy_n(x.args.begin(), 3, y.args.begin());
            }
            const auto near = [](double u, double v) { return std::fabs(u - v) < 1e-6; };
            if (!(near(x.args[0], y.args[0]) && near(x.args[1], y.args[1]) &&
                  near(x.args[2], y.args[2]))) {
                const std::optional<std::string> m = blend_as_matrices({x}, {y}, p);
                if (!m) { return std::nullopt; }
                out += *m;
                continue;
            }
            x.args[3] = lerp(x.args[3], y.args[3]);
            out += function_text(x);
            continue;
        }
        transform_fn fn = x;
        for (std::size_t k = 0; k < fn.args.size(); ++k) {
            fn.args[k] = lerp(x.args[k], y.args[k]);
            fn.pct[k] = lerp(x.pct[k], y.pct[k]);
        }
        // A perspective's reciprocal depth past `none` is `none`.
        if (fn.name == "perspective") { fn.args[0] = std::max(fn.args[0], 0.0); }
        // Two one-axis functions of one name stay that function (CSS
        // Transforms 1 §12: a pair of the same function interpolates its
        // arguments), `translateY(70%)` to `translateY(90%)` passing
        // through `translateY(80%)` rather than its two-argument spelling,
        // and `scaleZ(1)` to `scaleZ(2)` through `scaleZ(1.5)`.
        const std::string & spelled = x.spelled;
        const char last = spelled.empty() ? '\0' : spelled.back();
        const bool one_axis = (spelled.starts_with("translate") || spelled.starts_with("scale")) &&
                              (last == 'x' || last == 'y' || last == 'z');
        if (one_axis && spelled == y.spelled && is_3d(fn) == (last == 'z')) {
            const std::size_t axis = last == 'x' ? 0 : last == 'y' ? 1 : 2;
            transform_fn single{fn.name, {fn.args[axis]}, {fn.pct[axis]}, {}};
            std::string text = function_text(single);
            const std::size_t open = text.find('(');
            const std::string name =
                std::string{spelled.starts_with("translate") ? "translate" : "scale"} +
                static_cast<char>(std::toupper(static_cast<unsigned char>(last)));
            out += name + text.substr(open);
            continue;
        }
        out += function_text(fn);
    }
    if (prefix < a->size() || prefix < b->size()) {
        const std::vector<transform_fn> x(a->begin() + static_cast<std::ptrdiff_t>(prefix),
                                          a->end());
        const std::vector<transform_fn> y(b->begin() + static_cast<std::ptrdiff_t>(prefix),
                                          b->end());
        const std::optional<std::string> rest = blend_lists(x, y, p);
        if (!rest) { return unblended(x, y); }
        out += (out.empty() ? "" : " ") + *rest;
    }
    return out;
}

// A filter list in computed form - percentages as numbers, lengths in px -
// as its functions; `none` is the empty list; nothing for a list this does
// not model (a `url()`).
[[nodiscard]] std::optional<std::vector<filter_fn>> parse_filters(std::string_view text) {
    std::vector<filter_fn> out;
    if (text.empty() || ascii_iequals(text, "none")) { return out; }
    const std::string computed = css::computed_filter(text, {});
    for (const std::string_view raw :
         split_top_level(computed.empty() ? text : computed, html_whitespace)) {
        const std::string_view item = trim(raw, html_whitespace);
        if (item.empty()) { continue; }
        const std::size_t open = item.find('(');
        if (open == std::string_view::npos || !item.ends_with(')')) { return std::nullopt; }
        filter_fn fn{
            ascii_lower_copy(item.substr(0, open)),
            std::string{trim(item.substr(open + 1, item.size() - open - 2), html_whitespace)}};
        if (fn.name == "url" || fn.name == "src") { return std::nullopt; }
        out.push_back(std::move(fn));
    }
    return out;
}

// The value a missing or empty function argument means (Filter Effects 1
// §11.2's lacuna value).
[[nodiscard]] std::string_view filter_identity(std::string_view name) {
    if (name == "blur") { return "0px"; }
    if (name == "hue-rotate") { return "0deg"; }
    if (name == "drop-shadow") { return "rgba(0, 0, 0, 0) 0px 0px 0px"; }
    if (name == "grayscale" || name == "invert" || name == "sepia") { return "0"; }
    return "1"; // Brightness, contrast, opacity, saturate.
}

// Filter Effects 1 §11.2: function by function while the lists match, the shorter padded
// with the missing functions' lacuna values; a drop-shadow is a shadow. No
// result goes negative, and the four amounts that saturate at 1 stop there.
[[nodiscard]] std::optional<std::string> interpolate_filter(std::string_view from,
                                                            std::string_view to, double p,
                                                            const css::length_context & ctx) {
    std::optional<std::vector<filter_fn>> a = parse_filters(from);
    std::optional<std::vector<filter_fn>> b = parse_filters(to);
    if (!a || !b) { return std::nullopt; }
    if (a->empty() && b->empty()) { return "none"; }
    for (std::size_t i = 0; i < std::min(a->size(), b->size()); ++i) {
        if ((*a)[i].name != (*b)[i].name) { return std::nullopt; }
    }
    for (std::size_t i = a->size(); i < b->size(); ++i) { a->push_back({(*b)[i].name, ""}); }
    for (std::size_t i = b->size(); i < a->size(); ++i) { b->push_back({(*a)[i].name, ""}); }
    std::string out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        const std::string & name = (*a)[i].name;
        const std::string_view x = (*a)[i].args.empty() ? filter_identity(name) : (*a)[i].args;
        const std::string_view y = (*b)[i].args.empty() ? filter_identity(name) : (*b)[i].args;
        bool ok = true;
        std::string piece;
        if (name == "drop-shadow") {
            piece = interpolate_pair("text-shadow", computed_shape("text-shadow", x, ctx),
                                     computed_shape("text-shadow", y, ctx), p, ctx, ok);
        } else {
            const std::optional<numeric_pair> n = numeric_of(x, y, ctx);
            if (!n) { return std::nullopt; }
            css::calc_result mixed = mix(n->a, n->b, p);
            // Every amount but an angle stops at zero: hue-rotate(0deg) to
            // hue-rotate(180deg) at -0.5 is -90deg (WPT filter-interpolation-001).
            if (name != "hue-rotate") { mixed.px = std::max(mixed.px, 0.0); }
            if (name == "grayscale" || name == "invert" || name == "opacity" || name == "sepia") {
                mixed.px = std::min(mixed.px, 1.0);
            }
            piece = css::serialize_calc(mixed);
        }
        if (!ok) { return std::nullopt; }
        if (i != 0) { out += ' '; }
        out += name + '(' + piece + ')';
    }
    return out;
}

// Filter Effects 1 §11.3, accumulation of two filter lists: each extended
// with the lacuna values of the other's trailing functions, then function by
// function - a length, an angle or a drop-shadow sums, an amount whose lacuna
// is 0 (grayscale, invert, sepia) sums, one whose lacuna is 1 (brightness,
// contrast, opacity, saturate) is `a + b - 1`. Unclamped: the interpolation
// that follows clamps. nullopt - the keyframe's list replaces - for a url()
// or two functions that do not pair.
[[nodiscard]] std::optional<std::string> accumulate_filter(std::string_view underlying,
                                                           std::string_view value,
                                                           const css::length_context & ctx) {
    std::optional<std::vector<filter_fn>> a = parse_filters(underlying);
    std::optional<std::vector<filter_fn>> b = parse_filters(value);
    if (!a || !b) { return std::nullopt; }
    if (a->empty()) { return std::string{value}; }
    for (std::size_t i = 0; i < std::min(a->size(), b->size()); ++i) {
        if ((*a)[i].name != (*b)[i].name) { return std::nullopt; }
    }
    for (std::size_t i = a->size(); i < b->size(); ++i) { a->push_back({(*b)[i].name, ""}); }
    for (std::size_t i = b->size(); i < a->size(); ++i) { b->push_back({(*a)[i].name, ""}); }
    std::string out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        const std::string & name = (*a)[i].name;
        const std::string_view x = (*a)[i].args.empty() ? filter_identity(name) : (*a)[i].args;
        const std::string_view y = (*b)[i].args.empty() ? filter_identity(name) : (*b)[i].args;
        std::string piece;
        if (name == "drop-shadow") {
            piece = add_pair("text-shadow", computed_shape("text-shadow", x, ctx),
                             computed_shape("text-shadow", y, ctx), ctx);
        } else {
            const std::optional<numeric_pair> n = numeric_of(x, y, ctx);
            if (!n) { return std::nullopt; }
            css::calc_result total = sum(n->a, n->b);
            if (filter_identity(name) == "1") { total.px -= 1; }
            piece = css::serialize_calc(total);
        }
        if (i != 0) { out += ' '; }
        out += name + '(' + piece + ')';
    }
    return out;
}

// Two whole 2D lists accumulated as matrices (CSS Transforms 2 §16, for lists
// that do not pair function by function): each decomposed into a translation,
// a rotation, a skew - the shear of the rows once scaled out, which is the 2D
// case of the 3D decomposition - and a scale; the translations, angles and
// skews summed, the scales each summed less one, and recomposed.
// `translateX(100px) rotate(90deg)` accumulating `translateX(100px)` is
// `matrix(0, 1, -1, 0, 200, 0)` (WPT transform-translate-composition). nullopt
// for a percentage, a 3D list or a singular matrix.
[[nodiscard]] std::optional<std::string> accumulate_as_matrices(
    const std::vector<transform_fn> & a, const std::vector<transform_fn> & b) {
    for (const std::vector<transform_fn> * list : {&a, &b}) {
        for (const transform_fn & fn : *list) {
            if (std::ranges::any_of(fn.pct, [](double v) { return v != 0; })) {
                return std::nullopt;
            }
        }
    }
    const matrix3d x = matrix3d_of(a), y = matrix3d_of(b);
    if (!flat(x) || !flat(y)) { return std::nullopt; }
    struct parts {
        double tx, ty, sx, sy, skew, angle;
    };
    const auto split = [](const matrix3d & m) -> std::optional<parts> {
        double r0x = m[0], r0y = m[1], r1x = m[4], r1y = m[5];
        parts out{m[12], m[13], std::hypot(r0x, r0y), 0, 0, 0};
        if (out.sx == 0) { return std::nullopt; }
        r0x /= out.sx, r0y /= out.sx;
        out.skew = r0x * r1x + r0y * r1y;
        r1x -= out.skew * r0x, r1y -= out.skew * r0y;
        out.sy = std::hypot(r1x, r1y);
        if (out.sy == 0) { return std::nullopt; }
        r1x /= out.sy, r1y /= out.sy, out.skew /= out.sy;
        if (r0x * r1y - r0y * r1x < 0) {
            out.sx = -out.sx;
            r0x = -r0x, r0y = -r0y;
        }
        out.angle = std::atan2(r0y, r0x) * 180.0 / std::numbers::pi;
        return out;
    };
    const std::optional<parts> u = split(x), v = split(y);
    if (!u || !v) { return std::nullopt; }
    const double r = radians(u->angle + v->angle);
    matrix2d m{1, 0, 0, 1, u->tx + v->tx, u->ty + v->ty};
    m = multiply(m, {std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0});
    m = multiply(m, {1, 0, u->skew + v->skew, 1, 0, 0});
    m = multiply(m, {u->sx + v->sx - 1, 0, 0, u->sy + v->sy - 1, 0, 0});
    return matrix_text(m);
}

// CSS Transforms 2 §14, accumulation of two transform lists: function by
// function when the lists match in length and primitive - a translate sums,
// a scale sums its excess over 1, a rotate or skew sums its angles, a matrix
// pair is decomposed, summed the same way and recomposed - and otherwise the
// two whole lists as matrices, above; what neither can do is left to the
// caller, which appends. A matrix that cannot be decomposed (singular)
// leaves the keyframe's value alone.
[[nodiscard]] std::optional<std::string> accumulate_transform(std::string_view underlying,
                                                              std::string_view value) {
    std::optional<std::vector<transform_fn>> a = parse_transforms(underlying);
    std::optional<std::vector<transform_fn>> b = parse_transforms(value);
    if (!a || !b) { return std::nullopt; }
    if (a->empty()) { return std::string{value}; }
    if (b->empty()) { return std::string{underlying}; }
    for (std::size_t i = 0; i < std::min(a->size(), b->size()); ++i) {
        if (primitive_of((*a)[i].name) != primitive_of((*b)[i].name)) {
            return accumulate_as_matrices(*a, *b);
        }
    }
    // A shorter list that pairs as far as it goes is padded with identities,
    // as interpolation pads it: `rotate(45deg) skew(10deg, 20deg)` accumulating
    // `rotate(45deg)` is `rotate(90deg) skew(10deg, 20deg)`.
    for (std::size_t i = a->size(); i < b->size(); ++i) { a->push_back(identity_like((*b)[i])); }
    for (std::size_t i = b->size(); i < a->size(); ++i) { b->push_back(identity_like((*a)[i])); }
    std::string out;
    for (std::size_t i = 0; i < a->size(); ++i) {
        transform_fn x = (*a)[i];
        transform_fn y = (*b)[i];
        if (is_3d(x) != is_3d(y)) {
            promote(x);
            promote(y);
        }
        // ponytail: a perspective or a matrix3d() pair does not accumulate
        // here; summing the 3D decomposition is the upgrade if one must.
        if (x.name == "perspective" || x.name == "matrix3d") { return std::nullopt; }
        transform_fn fn = x;
        if (fn.name == "matrix") {
            const matrix2d mx = matrix_of(x), my = matrix_of(y);
            if (mx[0] * mx[3] - mx[1] * mx[2] == 0 || my[0] * my[3] - my[1] * my[2] == 0) {
                return std::string{value};
            }
            const decomposed2d dx = decompose(mx), dy = decompose(my);
            const decomposed2d sum{dx.tx + dy.tx,     dx.ty + dy.ty,       dx.sx + dy.sx - 1,
                                   dx.sy + dy.sy - 1, dx.angle + dy.angle, dx.m11 + dy.m11 - 1,
                                   dx.m12 + dy.m12,   dx.m21 + dy.m21,     dx.m22 + dy.m22 - 1};
            if (i != 0) { out += ' '; }
            out += matrix_text(recompose(sum));
            continue;
        }
        if (fn.name == "rotate3d") {
            // About one axis the angles sum; about two it is the matrices'.
            const auto unit = [](transform_fn & r) {
                const double n = std::hypot(r.args[0], r.args[1], r.args[2]);
                if (n == 0) { return; }
                for (std::size_t k = 0; k < 3; ++k) { r.args[k] /= n; }
            };
            unit(x), unit(y);
            if (x.args[3] == 0) { std::copy_n(y.args.begin(), 3, x.args.begin()); }
            if (y.args[3] == 0) { std::copy_n(x.args.begin(), 3, y.args.begin()); }
            for (std::size_t k = 0; k < 3; ++k) {
                if (std::fabs(x.args[k] - y.args[k]) >= 1e-6) { return std::nullopt; }
            }
            fn = x;
            fn.args[3] = x.args[3] + y.args[3];
            if (i != 0) { out += ' '; }
            out += function_text(fn);
            continue;
        }
        const bool scale = fn.name == "scale" || fn.name == "scale3d";
        for (std::size_t k = 0; k < fn.args.size(); ++k) {
            fn.args[k] = scale ? x.args[k] + y.args[k] - 1 : x.args[k] + y.args[k];
            fn.pct[k] = x.pct[k] + y.pct[k];
        }
        if (i != 0) { out += ' '; }
        out += function_text(fn);
    }
    return out;
}

// `none`, `<angle>`, `x|y|z <angle>` or `<number>{3} <angle>`, the axis as
// given; nothing for anything else.
[[nodiscard]] std::optional<rotation> rotation_of(std::string_view text,
                                                  const css::length_context & ctx) {
    rotation out;
    if (ascii_iequals(text, "none")) { return out; }
    std::vector<std::string_view> parts;
    for (const std::string_view part : split_top_level(text, html_whitespace)) {
        if (!part.empty()) { parts.push_back(part); }
    }
    if (parts.empty() || parts.size() == 3 || parts.size() > 4) { return std::nullopt; }
    const css::math_answer angle = css::evaluate_math(parts.back(), ctx);
    if (angle.outcome != css::math_outcome::resolved ||
        angle.value.type != css::numeric_type::angle) {
        return std::nullopt;
    }
    out.angle = angle.value.px;
    if (parts.size() == 2) {
        const std::string axis = ascii_lower_copy(parts[0]);
        if (axis == "x") {
            out.x = 1, out.z = 0;
        } else if (axis == "y") {
            out.y = 1, out.z = 0;
        } else if (axis != "z") {
            return std::nullopt;
        }
    } else if (parts.size() == 4) {
        double v[3];
        for (std::size_t i = 0; i < 3; ++i) {
            const std::optional<double> n = number_of(parts[i]);
            if (!n) { return std::nullopt; }
            v[i] = *n;
        }
        out.x = v[0], out.y = v[1], out.z = v[2];
    }
    return out;
}

// Interpolates two `rotate` property values. Rotations about one axis (or
// where one side is a rotation by nothing, which takes the other's axis)
// interpolate by angle; rotations about different axes are slerped as unit
// quaternions (CSS Transforms 2 §9).
[[nodiscard]] std::optional<std::string> interpolate_rotate(std::string_view from,
                                                            std::string_view to, double p,
                                                            const css::length_context & ctx) {
    std::optional<rotation> a = rotation_of(from, ctx);
    std::optional<rotation> b = rotation_of(to, ctx);
    if (!a || !b) { return std::nullopt; }
    const auto unit = [](rotation & r) {
        const double n = std::hypot(r.x, r.y, r.z);
        if (n == 0) { return false; }
        r.x /= n, r.y /= n, r.z /= n;
        return true;
    };
    if (!unit(*a) || !unit(*b)) { return std::nullopt; }
    if (a->angle == 0) { a->x = b->x, a->y = b->y, a->z = b->z; }
    if (b->angle == 0) { b->x = a->x, b->y = a->y, b->z = a->z; }
    const auto text = [](double x, double y, double z, double angle) {
        const auto tidy = [](double v) { return std::fabs(v) < 5e-7 ? 0.0 : v; }; // No `-0`.
        return css::serialize_number(tidy(x)) + " " + css::serialize_number(tidy(y)) + " " +
               css::serialize_number(tidy(z)) + " " + css::serialize_number(tidy(angle)) + "deg";
    };
    const auto near = [](double u, double v) { return std::fabs(u - v) < 1e-6; };
    // A whole number of turns about one axis, against another axis, is a
    // rotation by nothing too, and takes the other's axis: `1 2 3 360deg` to
    // `0 1 0 100deg` is 200deg about y at 2 (WPT rotate-composition).
    // At its own end that nothing is the identity, which is `0deg` about z.
    if (!(near(a->x, b->x) && near(a->y, b->y) && near(a->z, b->z))) {
        if (std::fmod(a->angle, 360.0) == 0) {
            if (p == 0) { return text(0, 0, 1, 0); }
            *a = rotation{b->x, b->y, b->z, 0};
        } else if (std::fmod(b->angle, 360.0) == 0) {
            if (p == 1) { return text(0, 0, 1, 0); }
            *b = rotation{a->x, a->y, a->z, 0};
        }
    }
    if (near(a->x, b->x) && near(a->y, b->y) && near(a->z, b->z)) {
        return text(a->x, a->y, a->z, (1 - p) * a->angle + p * b->angle);
    }
    // Different axes: the two rotations as unit quaternions, slerped
    // (CSS Transforms 2 §9's interpolation of rotate3d()), then back to an
    // axis and an angle.
    const auto quaternion = [](const rotation & r) {
        const double half = radians(r.angle) / 2;
        return std::array<double, 4>{r.x * std::sin(half), r.y * std::sin(half),
                                     r.z * std::sin(half), std::cos(half)};
    };
    std::array<double, 4> q1 = quaternion(*a), q2 = quaternion(*b);
    double dot = q1[0] * q2[0] + q1[1] * q2[1] + q1[2] * q2[2] + q1[3] * q2[3];
    if (dot < 0) {
        for (double & v : q2) { v = -v; }
        dot = -dot;
    }
    dot = std::min(dot, 1.0);
    const double theta = std::acos(dot);
    std::array<double, 4> q;
    for (std::size_t i = 0; i < 4; ++i) {
        q[i] = theta < 1e-6 ? (1 - p) * q1[i] + p * q2[i]
                            : (std::sin((1 - p) * theta) * q1[i] + std::sin(p * theta) * q2[i]) /
                                  std::sin(theta);
    }
    const double norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (double & v : q) { v /= norm; }
    // The canonical spelling of a rotation is the one under a half turn.
    if (q[3] < 0) {
        for (double & v : q) { v = -v; }
    }
    const double angle = 2 * std::acos(std::clamp(q[3], -1.0, 1.0));
    const double s = std::sin(angle / 2);
    if (std::fabs(s) < 1e-9) { return text(0, 0, 1, 0); }
    return text(q[0] / s, q[1] / s, q[2] / s, angle * 180.0 / std::numbers::pi);
}

} // namespace ctbrowser::style::easing_detail
