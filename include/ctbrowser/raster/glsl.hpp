#pragma once
#include <cstdint>
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
enum class base : std::uint8_t { f, i, b, void_, sampler2d, sampler_cube, struct_ };

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
    std::int32_t user = -1; // index into module::structs when kind is struct_
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
enum class storage : std::uint8_t { none, attribute, uniform, varying, constant };

// A parameter's direction. GLSL passes by value and copies `out` back on return.
enum class direction : std::uint8_t { in, out, inout };

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
enum class stage : std::uint8_t { vertex, fragment };

struct diagnostic {
    std::uint32_t line = 0;
    std::string message;
};

// A parsed shader.
//
// `ok` is the only thing a caller must check. Everything else is meaningful only
// when it is true - which is why the errors carry line numbers: they become
// getShaderInfoLog, and that is what a page shows a person.
struct module {
    bool ok = false;
    std::vector<diagnostic> errors;
    std::vector<node> nodes;
    std::vector<std::int32_t> declarations; // top-level, in source order
    std::vector<struct_type> structs;
    std::vector<interface_variable> interface_;
    stage which = stage::fragment;

    // The preprocessed text, kept for diagnostics: a message about line 40 of
    // something the author never saw is worse than no message.
    std::string preprocessed;

    [[nodiscard]] const node & at(std::int32_t i) const { return nodes[static_cast<std::size_t>(i)]; }
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
[[nodiscard]] module parse(std::string_view source, const options & how);

// The preprocessor on its own, exposed because it is separately testable and
// because #define/#if is where the surprises live. Errors land in `into`.
[[nodiscard]] std::string preprocess(std::string_view source, const options & how,
                                     std::vector<diagnostic> & into);

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
