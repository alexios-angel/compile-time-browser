#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/raster/gl.hpp>

// A PAGE'S WebGL CALLS, TRANSLATED TO GLES. Nothing else.
//
// The context this replaces kept a software mirror of GL state and separately,
// by hand, forwarded some calls to ANGLE; nineteen methods never forwarded at
// all and no test could see it. See docs/webgl-rewrite-plan.md - this file
// exists to make that shape impossible rather than to make it less likely.
//
// THE RULE: a method here translates and returns. It does not remember anything
// GL can be asked for. Where WebGL needs identity that GL does not provide - the
// `WebGLBuffer` object a page holds onto - a handle table stores the identity
// and NEVER the behaviour. The moment it caches what a buffer contains or which
// program is bound, it is the mirror again.
//
// BUILT GROUP BY GROUP against the 64 methods `webgl_bindings.cpp` calls, which
// is the checklist in docs/webgl-rewrite-plan.md. A method that is not written
// yet is a COMPILE ERROR at the binding site - which is the whole point, and the
// opposite of the silent no-op this replaces.

namespace ctbrowser::shell {

class webgl_context {
public:
    webgl_context(int width, int height, raster::gl::driver which = raster::gl::driver::fastest);

    [[nodiscard]] bool ok() const;
    // Why the context could not be made, for a page that asked for one and got
    // null. EGL fails before there is a GL error to read, so this is not
    // `take_error`'s job.
    [[nodiscard]] const std::string & device_error() const;

    // --- context and surface -------------------------------------------------

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    // The canvas bitmap, which is what the painter composites. Reading the
    // device into it is `present()`, and it is deliberately NOT done per draw:
    // the old context read back after every drawArrays, which is a full surface
    // copy per mesh.
    [[nodiscard]] paint::bitmap & surface();
    void present();

    // WHICH WebGL a page asked for. Kept because it is a fact about the PAGE's
    // request, not a mirror of driver state - `getContext('webgl')` and
    // `getContext('webgl2')` on the same canvas must not both succeed.
    void set_version(int version);
    [[nodiscard]] int version() const;

    void viewport(int x, int y, int width, int height);
    void scissor(int x, int y, int width, int height);
    void clear(std::uint32_t mask);
    void clear_color(float red, float green, float blue, float alpha);
    void clear_depth(float depth);
    void set_enabled(std::uint32_t capability, bool on);

    // --- the error contract, which is NOT the thing being deleted ------------
    //
    // Refusing a call BY NAME is the documented leniency contract in
    // docs/webgl2-plan.md, and it is the opposite of a silent no-op: a page can
    // read `getError`, and a report can list what this engine declined. Keeping
    // it is deliberate.
    [[nodiscard]] std::uint32_t take_error();
    void refuse(std::string_view call);
    [[nodiscard]] const std::vector<std::string> & refused() const;
    [[nodiscard]] const std::string & shader_error() const;

private:
    void fail(std::uint32_t error);

    raster::gl::device device_;
    paint::bitmap surface_;
    int version_ = 1;
    std::uint32_t error_ = 0;
    std::string shader_error_;
    std::vector<std::string> refused_;
};

} // namespace ctbrowser::shell
