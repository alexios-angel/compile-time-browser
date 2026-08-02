#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// GLSL ES, parsed. Stage one of docs/webgl-plan.md: preprocessor, lexer,
// parser, type model. Nothing here executes anything.
//
// WHY THIS EXISTS AT ALL. WebGL has no fixed pipeline: `drawArrays` runs a
// vertex shader per vertex and a fragment shader per fragment, and there is no
// path that draws anything without executing them. So a WebGL context cannot be
// stubbed the way a canvas can - the shaders ARE the renderer, and an
// implementation that accepts them and ignores them draws nothing while
// reporting success.
//
// THE SUBSET IS DECIDED BY A CORPUS, NOT BY TASTE. tests/glsl/ holds the sixteen
// shaders p5.js actually ships, extracted by tools/gen-glsl-fixtures.py, and
// they are what this has to parse. They are somebody else's GLSL, written
// without any knowledge of this implementation - which is the only kind worth
// testing a parser against. Reading them is what added the preprocessor,
// structs, function overloading and uniform arrays to the plan.
//
// The dialect is GLSL ES 1.00 (WebGL 1). p5 asks for a WebGL 2 context first and
// falls back, and this engine only ever offers WebGL 1, so `WEBGL2` is undefined
// when its preamble is preprocessed and its `IN`/`OUT` macros expand to
// `attribute`/`varying`. Supporting 3.00 would be a second language for no
// caller.

namespace ctbrowser::raster::glsl {

// --- types -----------------------------------------------------------------

// The base of a type. `void_` is a real one: it is what a function returns.
enum class base : std::uint8_t {
    f,
    i,
    b,
    void_,
    sampler2d,
    sampler_cube,
    struct_
};

// A GLSL type: a base, a shape, and possibly an array length.
//
// ONE STRUCT FOR SCALARS, VECTORS AND MATRICES, because GLSL's operators are
// defined uniformly over them - `a * b` means six different things depending on
// the shapes, and deciding in one place beats a hierarchy that re-implements
// each combination.
//
// Matrices are COLUMN-MAJOR, which is what `mat[i]` means in GLSL and what
// `uniformMatrix4fv` hands over. Transposing it looks like a plausible wrong
// rotation rather than an error, so it is stated here and tested in stage 2.
struct type {
    base kind = base::f;
    std::uint8_t rows = 1;  // components of a vector, or rows of a matrix
    std::uint8_t cols = 1;  // 1 unless this is a matrix
    std::int32_t user = -1; // index into shader::structs when kind is struct_
    // 0 when not an array. -1 means `float x[];` - a size the declaration did
    // not give, which only a parameter or an unsized uniform may have.
    std::int32_t array = 0;

    [[nodiscard]] constexpr bool is_scalar() const noexcept { return rows == 1 && cols == 1; }
    [[nodiscard]] constexpr bool is_vector() const noexcept { return rows > 1 && cols == 1; }
    [[nodiscard]] constexpr bool is_matrix() const noexcept { return cols > 1; }
    [[nodiscard]] constexpr bool is_sampler() const noexcept {
        return kind == base::sampler2d || kind == base::sampler_cube;
    }
    // How many floats one value occupies. An array multiplies it.
    [[nodiscard]] constexpr std::int32_t components() const noexcept {
        const std::int32_t one = static_cast<std::int32_t>(rows) * cols;
        return array > 0 ? one * array : one;
    }
    [[nodiscard]] constexpr bool operator==(const type &) const noexcept = default;
};

// The name GLSL calls it, for diagnostics and for getActiveUniform.
[[nodiscard]] std::string spell(const type & t);

// How a declaration reaches the shader. `none` is an ordinary local.
enum class storage : std::uint8_t {
    none,
    attribute,
    uniform,
    varying,
    constant,
    // A GLSL ES 3.00 fragment shader's declared colour output - `out vec4 c;`.
    // Not a varying: a varying in a fragment shader is an INPUT read from the
    // environment, and this is written by the shader and read by the
    // rasteriser. Conflating the two makes the output read as zero every frame
    // and the shader's writes go nowhere.
    fragment_output
};

// A parameter's direction. GLSL passes by value and copies `out` back on return.
enum class direction : std::uint8_t {
    in,
    out,
    inout
};

// --- the tree --------------------------------------------------------------

// Node kinds. A flat vector of nodes with integer child indices, the same shape
// the JavaScript compiler uses - it keeps the tree in one allocation and makes
// a node index a stable name that survives the vector growing.
enum class nk : std::uint8_t {
    // expressions
    literal,    // text is the source spelling; `t` is its type
    identifier, // text
    field,      // a.text  - a struct member OR a swizzle, told apart in stage 2
    index,      // a[b]
    call,       // text(kids...) - a constructor is a call whose name is a type
    unary,      // text a        - ! - + ~
    prefix,     // ++a --a
    postfix,    // a++ a--
    binary,     // a text b
    ternary,    // a ? b : c
    assign,     // a text b      - = += -= *= /= %= etc
    sequence,   // a , b

    // statements
    block,       // kids
    var_decl,    // text = name, a = initialiser, t = type, storage in `store`
    expr_stmt,   // a
    if_stmt,     // a ? b : c, c may be absent
    for_stmt,    // a init; b cond; c step; d body
    while_stmt,  // a cond, b body
    do_stmt,     // a body, b cond
    return_stmt, // a, may be absent
    break_stmt,
    continue_stmt,
    discard_stmt,

    // declarations
    function,  // text = name, t = return type, kids = params, a = body (-1 if a prototype)
    parameter, // text = name, t = type, `dir` is its direction
    struct_def // text = name, kids = member var_decls
};

struct node {
    nk kind = nk::literal;
    type t;
    std::string text;
    // A LITERAL'S VALUE, PARSED ONCE, when the node is built.
    //
    // `literal()` in the evaluator used to call std::strtof(n.text.c_str()) on
    // EVERY evaluation, so the `0.5` written in a shader was re-parsed from its
    // characters millions of times per draw - 5.4% of the shader benchmark,
    // measured with callgrind.
    //
    // It was also a DETERMINISM bug, which matters more. strtof and strtol
    // respect LC_NUMERIC, so on a host whose locale writes decimals with a
    // comma the same shader would produce different pixels - and this
    // repository byte-compares its renders across Linux and the Windows cross
    // build. std::from_chars is locale-independent by definition.
    float number = 0.0f;
    std::int32_t integer = 0;
    std::int32_t a = -1;
    std::int32_t b = -1;
    std::int32_t c = -1;
    std::int32_t d = -1;
    std::vector<std::int32_t> kids;
    std::uint32_t line = 0;
    storage store = storage::none;
    direction dir = direction::in;
};

// A struct type the shader declared.
struct struct_type {
    std::string name;
    struct member {
        std::string name;
        type t;
    };
    std::vector<member> members;
};

// What the WebGL context needs to know about a shader without executing it:
// what it takes in and what it hands on. `getAttribLocation`,
// `getActiveUniform` and the varying interpolation all read this.
struct interface_variable {
    std::string name;
    type t;
    storage store = storage::none;
    std::uint32_t line = 0;
};

// --- the result ------------------------------------------------------------

// Which shader this is. It decides more than it looks: a fragment shader has no
// `gl_Position` and a vertex shader has no `gl_FragColor`, and p5's own preamble
// branches on `FRAGMENT_SHADER` to decide what `IN` means.
enum class stage : std::uint8_t {
    vertex,
    fragment
};

struct diagnostic {
    std::uint32_t line = 0;
    std::string message;
};

// A parsed shader.
//
// NAMED `shader` AND NOT `module`, and that is not taste. A line beginning with
// `module` at namespace scope is a C++20 MODULE DECLARATION as far as clang is
// concerned - it is a context-sensitive keyword there - so `module parse(...)`
// and even `module out;` are syntax errors before they are ever declarations.
// libstdc++ and gcc let it through; the mingw cross-build did not, which is how
// it was found. `shader` beside `program` is also the naming GL itself uses: a
// shader is compiled source, a program is what runs.
//
// `ok` is the only thing a caller must check. Everything else is meaningful only
// when it is true - which is why the errors carry line numbers: they become
// getShaderInfoLog, and that is what a page shows a person.
struct shader {
    bool ok = false;
    std::vector<diagnostic> errors;
    std::vector<node> nodes;
    std::vector<std::int32_t> declarations; // top-level, in source order
    std::vector<struct_type> structs;
    std::vector<interface_variable> interface_;
    stage which = stage::fragment;

    // GLSL ES 3.00 rather than 1.00, selected by `#version 300 es`.
    //
    // The directive was recorded and ignored until 2026-08-02, which was honest
    // when this was a WebGL 1 implementation and said so. It selects a
    // LANGUAGE, so ignoring it is the one directive that cannot keep being
    // ignored once ES 3.00 shaders are accepted at all.
    bool es300 = false;

    // WHICH VARIABLE THE FRAGMENT COLOUR COMES OUT OF.
    //
    // ES 1.00 writes the built-in `gl_FragColor`, and the rasteriser looked for
    // that name outright. ES 3.00 declares its own output - `out vec4 colour;`
    // - and may not contain the identifier `gl_FragColor` anywhere, so a
    // hardcoded lookup finds nothing and the draw paints NOTHING while
    // compiling and linking perfectly. That is the silent-wrong-answer shape
    // this tree keeps paying for, so the name lives here and softgl asks.
    std::string fragment_output = "gl_FragColor";

    // The preprocessed text, kept for diagnostics: a message about line 40 of
    // something the author never saw is worse than no message.
    std::string preprocessed;

    [[nodiscard]] const node & at(std::int32_t i) const {
        return nodes[static_cast<std::size_t>(i)];
    }
    // One message per line, in source order - the shape glGetShaderInfoLog has.
    [[nodiscard]] std::string info_log() const;
};

// Options a caller sets before parsing. p5 needs both of these: it prepends a
// preamble that branches on FRAGMENT_SHADER, and defines WEBGL2 only when it got
// a WebGL 2 context.
struct options {
    stage which = stage::fragment;
    std::vector<std::pair<std::string, std::string>> defines;
};

// Preprocess and parse. Never throws and never asserts on bad input: a shader is
// UNTRUSTED text from a page, so every malformed one has to come back as a
// diagnostic rather than as a crash.
[[nodiscard]] shader parse(std::string_view source, const options & how);

// The preprocessor on its own, exposed because it is separately testable and
// because #define/#if is where the surprises live. Errors land in `into`.
// `es300` reports whether the source said `#version 300 es`. An out-parameter
// rather than a second return, because every existing caller wants only the
// text and this keeps them unchanged.
[[nodiscard]] std::string preprocess(std::string_view source, const options & how,
                                     std::vector<diagnostic> & into, bool * es300 = nullptr);

// --- running one --------------------------------------------------------
//
// Stage two of docs/webgl-plan.md: the REFERENCE evaluator. A plain tree walker,
// deliberately the simplest thing that is correct, because it is the ORACLE -
// the bytecode VM in stage three is differentially tested against it, and a fast
// implementation that is only checked against itself proves nothing.
//
// It is also the slowest thing here by design. The benchmark in tests/ reports
// what it costs, so stage three has a number to beat rather than an impression.

// The components of a value, with INLINE STORAGE.
//
// THE MEASUREMENT THAT MADE THIS EXIST. With a `std::vector<float>` here, every
// arithmetic node in a shader heap-allocated and freed a result - and the cost
// per node was 0.24 microseconds, of which the arithmetic was almost none: a
// hundred vec4 operations cost only 15% more than a hundred SCALAR ones, so four
// times the data was nearly free while the container around it was everything.
//
// Sixteen floats inline is a mat4, which is the largest thing GLSL has that is
// not an array or a struct - so a shader that touches neither never allocates.
// Those two spill to the heap, which is right: they are rare and they are large.
//
// Only the operations the evaluator uses, deliberately. This is not a general
// container and should not grow into one.
class components {
public:
    static constexpr std::size_t inline_capacity = 16;

    components() = default;
    components(std::initializer_list<float> from) { assign_range(from.begin(), from.end()); }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] float * data() noexcept { return spilled() ? spill_.data() : inline_.data(); }
    [[nodiscard]] const float * data() const noexcept {
        return spilled() ? spill_.data() : inline_.data();
    }
    [[nodiscard]] float & operator[](std::size_t i) noexcept { return data()[i]; }
    [[nodiscard]] const float & operator[](std::size_t i) const noexcept { return data()[i]; }
    [[nodiscard]] float * begin() noexcept { return data(); }
    [[nodiscard]] float * end() noexcept { return data() + size_; }
    [[nodiscard]] const float * begin() const noexcept { return data(); }
    [[nodiscard]] const float * end() const noexcept { return data() + size_; }
    [[nodiscard]] float & front() noexcept { return data()[0]; }
    [[nodiscard]] const float & front() const noexcept { return data()[0]; }

    void clear() noexcept {
        size_ = 0;
        spill_.clear();
    }
    void push_back(float f) {
        grow_to(size_ + 1);
        data()[size_ - 1] = f;
    }
    void assign(std::size_t count, float f) {
        clear();
        grow_to(count);
        for (std::size_t i = 0; i < count; ++i) { data()[i] = f; }
    }
    void resize(std::size_t count, float f = 0.0f) {
        const std::size_t was = size_;
        grow_to(count);
        for (std::size_t i = was; i < count; ++i) { data()[i] = f; }
    }
    void reserve(std::size_t count) {
        if (count > inline_capacity) { spill_.reserve(count); }
    }
    template <typename It> void assign_range(It first, It last) {
        clear();
        for (It at = first; at != last; ++at) { push_back(*at); }
    }
    template <typename It> void append(It first, It last) {
        for (It at = first; at != last; ++at) { push_back(*at); }
    }

private:
    [[nodiscard]] bool spilled() const noexcept { return size_ > inline_capacity; }
    // Grow to `count`, moving to the heap the moment it no longer fits inline.
    void grow_to(std::size_t count) {
        if (count > inline_capacity) {
            if (!spilled() || spill_.size() < size_) {
                // Crossing the boundary: carry what is already there across.
                std::vector<float> moved(inline_.begin(),
                                         inline_.begin() + static_cast<std::ptrdiff_t>(
                                                               std::min(size_, inline_capacity)));
                moved.resize(count, 0.0f);
                spill_ = std::move(moved);
            } else {
                spill_.resize(count, 0.0f);
            }
        }
        size_ = count;
    }

    std::size_t size_ = 0;
    std::array<float, inline_capacity> inline_{};
    std::vector<float> spill_;
};

// A runtime value: a type and its components, flat.
//
// FLAT ON PURPOSE. A vec3 is three floats, a mat4 is sixteen in COLUMN-MAJOR
// order, an array is its elements end to end, and a struct is its members end to
// end. One representation for all of them means member access, indexing and
// swizzling are all the same operation - pick a range - instead of four.
struct value {
    type t;
    components v;

    [[nodiscard]] static value scalar(float f) {
        return value{type{base::f, 1, 1, -1, 0}, {f}};
    }
    [[nodiscard]] static value integer(int i) {
        return value{type{base::i, 1, 1, -1, 0}, {static_cast<float>(i)}};
    }
    [[nodiscard]] static value boolean(bool b) {
        return value{type{base::b, 1, 1, -1, 0}, {b ? 1.0f : 0.0f}};
    }
    [[nodiscard]] static value vector(std::initializer_list<float> parts) {
        return value{type{base::f, static_cast<std::uint8_t>(parts.size()), 1, -1, 0}, parts};
    }
    [[nodiscard]] float f(std::size_t i = 0) const { return i < v.size() ? v[i] : 0.0f; }
    [[nodiscard]] int i(std::size_t at = 0) const { return static_cast<int>(f(at)); }
    [[nodiscard]] bool truthy() const { return f(0) != 0.0f; }
};

// LOOKING UP BY string_view WITHOUT BUILDING A string.
//
// Every identifier a shader reads is looked up by name, and the callers all keep
// their inputs in a `std::string`-keyed map - so `find(std::string{name})`
// allocated once per identifier per fragment. Transparent hashing removes the
// allocation and changes nothing else.
struct string_hash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept {
        return std::hash<std::string_view>{}(text);
    }
    [[nodiscard]] std::size_t operator()(const std::string & text) const noexcept {
        return std::hash<std::string_view>{}(text);
    }
};

// What a shader sees that is not written in it.
//
// `read` is how uniforms, attributes and varyings arrive, and how gl_FragCoord
// and friends do - the caller decides what exists, because the rasteriser knows
// and the language does not. `sample` is a texture fetch: a unit index and a
// coordinate in, RGBA in 0..1 out. Kept as a callback so this file knows nothing
// about how textures are stored.
struct environment {
    std::function<const value *(std::string_view)> read;
    std::function<value(int unit, float s, float t)> sample;
};

struct execution {
    bool ok = false;
    // NOT AN ERROR. A fragment shader that ran `discard` did exactly what it was
    // asked; reporting it through `ok` would confuse it with one that failed.
    bool discarded = false;
    std::vector<std::pair<std::string, value>> outputs;
    std::string error;

    [[nodiscard]] const value * find(std::string_view name) const;
};

// A shader PREPARED TO RUN MANY TIMES.
//
// THE MEASUREMENT THAT MADE THIS EXIST. `execute` below builds the function
// table, evaluates every global initialiser and constructs the globals map on
// every call - which is once per FRAGMENT. Adding twenty constants and twenty
// functions that `main` never touches made the same shader run 3.9x slower
// (1218k -> 313k fragments/sec), so for a real shader most of the time was
// setup rather than evaluation.
//
// A `program` does that work once. Per fragment it resets the globals from a
// template and runs `main`, which is a vector copy rather than a rebuild.
//
// It is also the shape the bytecode VM needs (docs/webgl-plan.md stage 3): the
// split between "prepare once" and "run per fragment" is the same either way,
// so this is the first half of that work rather than a detour around it.
class program {
public:
    explicit program(const shader & m);
    ~program();
    program(program &&) noexcept;
    program & operator=(program &&) noexcept;
    program(const program &) = delete;
    program & operator=(const program &) = delete;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::string & error() const noexcept;

    // Run `main` with these inputs. Cheap enough to call per fragment, which is
    // the entire point.
    [[nodiscard]] execution run(const environment & env) const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// Run `main` once. Every runtime failure - an unknown name, an index out of
// range, a call that resolves to nothing - comes back in `error` rather than as
// a crash, for the same reason the parser's do: this is a page's text.
//
// Prepares and runs in one go, so a caller that draws more than one fragment
// should build a `program` instead and keep it.
[[nodiscard]] execution execute(const shader & m, const environment & env);

// NOT IMPLEMENTED, named here rather than discovered at run time:
//
//   * interface blocks (`uniform Block { ... }`) - a GLSL ES 3.00 feature, and
//     no shader in the corpus has one
//   * arrays of arrays, and arrays of structs
//   * `#include` - not in the language; p5 concatenates its own sources
//   * `#line` is parsed and ignored, so a shader that renumbers itself gets
//     diagnostics against the real line rather than the claimed one
//   * precision qualifiers are parsed and IGNORED: everything here is a float
//     underneath, so `highp int` beyond 2^24 loses its low bits

} // namespace ctbrowser::raster::glsl
