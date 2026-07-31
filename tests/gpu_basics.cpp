// ctbrowser.gpu: the SDL3 backend, and the fallback when it is not there.
//
// The claim worth testing is not "the GPU path runs" - it is that IT AGREES
// WITH THE SOFTWARE PATH. Two renderers that disagree are two browsers, and the
// software one is the reference precisely because it is the one that always
// works. So the GPU result is downloaded and compared pixel for pixel.
//
// Agreement is achievable rather than aspirational because raster is SHARED:
// both backends draw display lists with ctbrowser.raster's :draw, and the GPU's
// job is only to composite the resulting tiles. Compositing is a 1:1 nearest
// blit with source-over blending, which the pipeline's blend state matches to
// the software blend_over.
//
// WHERE THERE IS NO DEVICE this test SKIPS rather than fails, and says so. CI
// runners have no GPU; a test that fails there teaches everyone to ignore it.
// The fallback itself is still tested, since that is the path those machines
// actually take.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/gpu/gpu.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/shell/shell.hpp> // shell::font8x8_metrics
#include <ctbrowser/style/style.hpp>

#include <SDL3/SDL.h>

#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::gpu::create_renderer;
using ctbrowser::gpu::renderer_preference;
using ctbrowser::gpu::sdl_gpu_backend;
using ctbrowser::paint::layer_tree;
using ctbrowser::paint::recorder;
using ctbrowser::raster::renderer;
using ctbrowser::raster::software_backend;
using ctbrowser::raster::surface;

namespace {

int skipped = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

void skip(std::string_view what, std::string_view why) {
    std::printf("SKIP %s (%s)\n", std::string{what}.c_str(), std::string{why}.c_str());
    ++skipped;
}

struct page {
    atom_table atoms;
    document doc{atoms};
    style::engine styles{atoms};
    style::style_map resolved;
    layout::box_node boxes;
    layout::fragment placed;
    layer_tree layers;

    void load(std::string_view html, std::string_view css, float viewport) {
        (void)parse_html(doc, html);
        styles.add_sheet(css, 1);
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
        layout::box_builder builder{atoms, resolved};
        boxes = builder.build(txn, txn.root());
        const layout::engine eng{shell::font8x8_metrics()};
        placed = eng.run(boxes, viewport);
        const recorder rec{atoms};
        layers = rec.record_layers(placed);
    }
};

void load_test_page(page & p, float viewport, bool translucent) {
    std::string css =
        "body { margin: 0; padding: 0; background-color: #ffffff } "
        ".a { background-color: #cc4020; color: #ffffff; font-size: 16px; padding: 4px } "
        ".b { background-color: #2040cc; color: #ffff00; font-size: 16px; padding: 4px } "
        ".c { background-color: #20a040; color: #000000; font-size: 8px; padding: 2px } ";
    if (translucent) { css += ".b { background-color: rgba(32, 64, 204, 0.5) } "; }
    p.load("<html><body><div class=a>alpha beta gamma delta epsilon</div>"
           "<div class=b>zeta eta theta iota kappa lambda mu</div>"
           "<div class=c>nu xi omicron pi rho sigma tau upsilon</div>"
           "<div class=a>phi chi psi omega and a few more words here</div></body></html>",
           css, viewport);
}

// How far apart two images are, per channel. Exact equality is the bar for
// opaque content; blending is float on the GPU and fixed-point on the CPU, so
// translucent content gets a tolerance instead of a pretence.
struct difference {
    std::size_t differing_pixels = 0;
    int worst_channel = 0;
};

[[nodiscard]] difference compare(const surface & a, const surface & b) {
    difference out;
    if (a.width() != b.width() || a.height() != b.height()) {
        out.differing_pixels = static_cast<std::size_t>(-1);
        return out;
    }
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const std::uint32_t p = a.row(y)[static_cast<std::size_t>(x)];
            const std::uint32_t q = b.row(y)[static_cast<std::size_t>(x)];
            if (p == q) { continue; }
            ++out.differing_pixels;
            for (int shift = 0; shift < 32; shift += 8) {
                const int d = std::abs(static_cast<int>((p >> shift) & 0xFFu) -
                                       static_cast<int>((q >> shift) & 0xFFu));
                out.worst_channel = d > out.worst_channel ? d : out.worst_channel;
            }
        }
    }
    return out;
}

[[nodiscard]] bool have_device() {
    auto probe = sdl_gpu_backend::create(16, 16);
    return probe.has_value();
}

// --- the fallback, which every machine exercises --------------------------

void test_fallback_when_the_gpu_is_refused() {
    // force_software is the CTBROWSER_RENDERER=software path: no device is even
    // attempted, and what comes back still renders.
    auto choice = create_renderer(64, 64, nullptr, 32, renderer_preference::force_software);
    check(static_cast<bool>(choice.renderer), "forcing software yields a renderer");
    check(!choice.hardware(), "which is not hardware");
    check(choice.fell_back_because.empty(), "and is not a fallback - it is what was asked for");

    page p;
    load_test_page(p, 64, false);
    check(raster::draw(choice.renderer, p.layers, nullptr, 32).has_value(),
          "and it renders a page");
}

void test_automatic_always_yields_something() {
    // The property that matters on a machine with no GPU: you still get a
    // browser. Whichever way this resolves here, it must resolve.
    auto choice = create_renderer(64, 64, nullptr, 32, renderer_preference::automatic);
    check(static_cast<bool>(choice.renderer), "automatic selection always yields a renderer");
    if (!choice.hardware()) {
        check(!choice.fell_back_because.empty(), "and when it falls back it says why");
        std::printf("     fell back to software: %s\n", choice.fell_back_because.c_str());
    } else {
        std::printf("     selected %s\n", std::string{choice.renderer.name()}.c_str());
    }
}

void test_force_gpu_reports_failure_rather_than_lying() {
    auto choice = create_renderer(64, 64, nullptr, 32, renderer_preference::force_gpu);
    if (have_device()) {
        check(static_cast<bool>(choice.renderer) && choice.hardware(),
              "force_gpu yields the hardware renderer where one exists");
        return;
    }
    // A caller that demanded hardware and cannot have it must be told, not
    // quietly handed software - that is how a bug report ends up describing the
    // wrong renderer.
    check(!static_cast<bool>(choice.renderer), "force_gpu yields nothing when there is no device");
    check(!choice.fell_back_because.empty(), "and explains why");
}

void test_environment_preference() {
    check(gpu::preference_from_environment() == renderer_preference::automatic ||
              std::getenv("CTBROWSER_RENDERER") != nullptr,
          "the default preference is automatic");
}

// --- the differential, where a device exists ------------------------------

void test_gpu_matches_software_on_opaque_content() {
    if (!have_device()) {
        skip("gpu vs software, opaque", "no SDL_GPU device here");
        return;
    }
    page p;
    load_test_page(p, 400, false);

    software_backend cpu{400, 300, 64};
    check(raster::draw(cpu, p.layers, nullptr, 64).has_value(), "the software frame draws");

    auto device = sdl_gpu_backend::create(400, 300, nullptr, 64);
    check(device.has_value(), "the GPU backend was created");
    if (!device) { return; }
    std::printf("     GPU driver: %s\n", device->driver().c_str());
    check(raster::draw(*device, p.layers, nullptr, 64).has_value(), "the GPU frame draws");

    const auto gpu_image = device->read_target();
    check(gpu_image.has_value(), "the GPU target reads back");
    if (!gpu_image) { return; }

    // Every pixel in this page is either fully opaque or untouched, so the
    // blend is exact on both sides and the images must be identical.
    const difference d = compare(cpu.target(), *gpu_image);
    check(d.differing_pixels == 0, "GPU and software agree byte for byte on opaque content");
    if (d.differing_pixels != 0) {
        std::printf("     %zu pixels differ, worst channel by %d\n", d.differing_pixels,
                    d.worst_channel);
    }
}

void test_gpu_matches_software_on_translucent_content() {
    if (!have_device()) {
        skip("gpu vs software, translucent", "no SDL_GPU device here");
        return;
    }
    page p;
    load_test_page(p, 400, true);

    software_backend cpu{400, 300, 64};
    check(raster::draw(cpu, p.layers, nullptr, 64).has_value(), "the software frame draws");
    auto device = sdl_gpu_backend::create(400, 300, nullptr, 64);
    if (!device) { return; }
    check(raster::draw(*device, p.layers, nullptr, 64).has_value(), "the GPU frame draws");
    const auto gpu_image = device->read_target();
    if (!gpu_image) { return; }

    // Here the two genuinely differ: the CPU blends in fixed point with a
    // rounding term, the GPU in floats. One step per channel is the honest bar;
    // anything more means the blend STATE is wrong, not the arithmetic.
    const difference d = compare(cpu.target(), *gpu_image);
    check(d.worst_channel <= 1, "GPU and software agree within one step on blended content");
    std::printf("     %zu of 120000 pixels differ, worst channel by %d\n", d.differing_pixels,
                d.worst_channel);
}

void test_gpu_scrolling_does_not_reraster() {
    if (!have_device()) {
        skip("gpu incremental raster", "no SDL_GPU device here");
        return;
    }
    page p;
    load_test_page(p, 300, false);
    auto device = sdl_gpu_backend::create(300, 200, nullptr, 64);
    if (!device) { return; }

    const rect viewport{0, 0, 300, 200};
    check(raster::draw(*device, p.layers, nullptr, 64, viewport).has_value(),
          "the first frame draws");

    // Same frame again: the incremental path has to hold on the GPU backend
    // too, or hardware acceleration is paid for by re-uploading the page.
    software_backend reference{300, 200, 64};
    check(raster::draw(reference, p.layers, nullptr, 64, viewport).has_value(), "reference draws");
    check(raster::draw(*device, p.layers, nullptr, 64, viewport).has_value(),
          "the repeated frame draws");
    const auto again = device->read_target();
    check(again.has_value() && compare(reference.target(), *again).differing_pixels == 0,
          "a repeated GPU frame is still the same image");
}

void test_gpu_through_the_compositor_thread() {
    if (!have_device()) {
        skip("gpu through the compositor thread", "no SDL_GPU device here");
        return;
    }
    page p;
    load_test_page(p, 400, false);
    auto device = sdl_gpu_backend::create(400, 300, nullptr, 64);
    if (!device) { return; }

    scheduler pool;
    software_backend reference{400, 300, 64};
    check(raster::draw(reference, p.layers, nullptr, 64).has_value(), "the reference frame draws");

    // The real arrangement: the device is touched only by the compositor
    // thread, raster runs on the pool, tiles arrive over the channels and are
    // uploaded as they land.
    raster::compositor_thread<sdl_gpu_backend> compositor{*device, pool};
    compositor.wait_for(compositor.submit(raster::frame_request{p.layers, rect{}, 64}));
    check(!compositor.last_error().has_value(), "the threaded GPU frame reported no error");

    const auto image = device->read_target();
    check(image.has_value() && compare(reference.target(), *image).differing_pixels == 0,
          "and the GPU compositor thread produces the software image exactly");
}

} // namespace

// DOES A REAL DRIVER TAKE THE SPIR-V THIS ENGINE EMITS?
//
// tests/spirv_basics.cpp checks the bytes structurally, which catches a wrong
// word count or an undefined id. It cannot catch a module that is well-formed
// and still invalid - a type mismatch, a missing decoration, an entry point
// whose interface does not list a variable it uses. Only a driver catches those,
// and a driver REJECTS them rather than misbehaving, which makes this a real
// test rather than a smoke test.
//
// It lives here because it needs SDL. The emitter itself needs nothing but the
// GLSL front end, which is why it is in raster/ - see raster/spirv.hpp.
void test_a_driver_accepts_our_spirv() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("     no SDL video: skipping the driver check\n");
        return;
    }
    if (!SDL_GPUSupportsShaderFormats(SDL_GPU_SHADERFORMAT_SPIRV, nullptr)) {
        std::printf("     no SPIR-V driver here: skipping\n");
        return;
    }
    SDL_GPUDevice * device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (device == nullptr) {
        std::printf("     no GPU device: skipping\n");
        return;
    }

    const auto build = [](const char * source, raster::glsl::stage which) {
        raster::glsl::options how;
        how.which = which;
        const raster::glsl::shader m = raster::glsl::parse(source, how);
        return raster::spirv::emit(m);
    };
    struct sample {
        const char * what;
        const char * source;
        raster::glsl::stage stage;
        SDL_GPUShaderStage sdl_stage;
    };
    for (const sample & each :
         {sample{"a vertex shader",
                 "attribute vec2 aPosition;\n"
                 "attribute vec4 aColor;\n"
                 "varying vec4 vColor;\n"
                 "void main() { vColor = aColor; gl_Position = vec4(aPosition, 0.0, 1.0); }",
                 raster::glsl::stage::vertex, SDL_GPU_SHADERSTAGE_VERTEX},
          sample{"a vertex shader with a matrix",
                 "attribute vec4 aPosition;\n"
                 "uniform mat4 uProjectionMatrix;\n"
                 "void main() { gl_Position = uProjectionMatrix * aPosition; }",
                 raster::glsl::stage::vertex, SDL_GPU_SHADERSTAGE_VERTEX},
          sample{"a fragment shader",
                 "varying vec4 vColor;\n"
                 "void main() { gl_FragColor = vec4(vColor.rgb, 1.0); }",
                 raster::glsl::stage::fragment, SDL_GPU_SHADERSTAGE_FRAGMENT}}) {
        const raster::spirv::module_binary binary = build(each.source, each.stage);
        check(binary.ok, std::string{"emitted "} + each.what + ": " + binary.error);
        if (!binary.ok) { continue; }

        SDL_GPUShaderCreateInfo info{};
        info.code = reinterpret_cast<const Uint8 *>(binary.bytes());
        info.code_size = binary.size_in_bytes();
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = each.sdl_stage;
        // One uniform buffer for the matrix sample; the driver checks the
        // counts against what the module actually declares.
        info.num_uniform_buffers = 0;
        SDL_GPUShader * shader = SDL_CreateGPUShader(device, &info);
        if (shader == nullptr) {
            std::printf("FAIL the driver rejected %s: %s\n", each.what, SDL_GetError());
            ++ctbrowser_test_failures;
            continue;
        }
        std::printf("     the driver accepted %s (%zu bytes)\n", each.what, binary.size_in_bytes());
        SDL_ReleaseGPUShader(device, shader);
    }
    // BUILDING A PIPELINE forces the SPIR-V through the driver's own compiler,
    // which is further than creating a shader gets. It is still not validation,
    // and that was measured rather than assumed - see the NOTE below and the
    // paragraph in docs/webgl-plan.md.
    //
    // It is worth having anyway: it proves the bytes survive the entire path
    // into a driver that will draw with them, which no structural check can.
    {
        const auto make_shader = [&](const char * source, raster::glsl::stage which,
                                     SDL_GPUShaderStage sdl_stage) -> SDL_GPUShader * {
            raster::glsl::options how;
            how.which = which;
            const raster::glsl::shader parsed = raster::glsl::parse(source, how);
            const raster::spirv::module_binary binary = raster::spirv::emit(parsed);
            if (!binary.ok) { return nullptr; }
            SDL_GPUShaderCreateInfo info{};
            info.code = reinterpret_cast<const Uint8 *>(binary.bytes());
            info.code_size = binary.size_in_bytes();
            info.entrypoint = "main";
            info.format = SDL_GPU_SHADERFORMAT_SPIRV;
            info.stage = sdl_stage;
            return SDL_CreateGPUShader(device, &info);
        };
        SDL_GPUShader * vertex =
            make_shader("attribute vec2 aPosition;\n"
                        "varying vec4 vColor;\n"
                        "void main() { vColor = vec4(1.0);"
                        " gl_Position = vec4(aPosition, 0.0, 1.0); }",
                        raster::glsl::stage::vertex, SDL_GPU_SHADERSTAGE_VERTEX);
        // The fragment shader mixes widths on purpose - `vec3 * float` and
        // `vec3 + float` - so the pipeline is compiling the operand-broadcast
        // path rather than only the easy instructions. A check that never
        // exercises the interesting code is not a check.
        SDL_GPUShader * fragment =
            make_shader("uniform float uScale;\n"
                        "varying vec4 vColor;\n"
                        "void main() {\n"
                        "  vec3 scaled = vColor.rgb * uScale;\n"
                        "  vec3 shifted = scaled + 0.25;\n"
                        "  gl_FragColor = vec4(normalize(shifted), 1.0);\n"
                        "}",
                        raster::glsl::stage::fragment, SDL_GPU_SHADERSTAGE_FRAGMENT);
        check(vertex != nullptr && fragment != nullptr, "both shaders were created");
        if (vertex != nullptr && fragment != nullptr) {
            SDL_GPUColorTargetDescription target{};
            target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = vertex;
            info.fragment_shader = fragment;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.target_info.num_color_targets = 1;
            info.target_info.color_target_descriptions = &target;
            SDL_GPUGraphicsPipeline * pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
            if (pipeline == nullptr) {
                std::printf("FAIL the driver would not build a pipeline from our SPIR-V: %s\n",
                            SDL_GetError());
                ++ctbrowser_test_failures;
            } else {
                std::printf("     THE DRIVER COMPILED OUR SPIR-V INTO A PIPELINE\n");
                SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
            }
        }
        if (vertex != nullptr) { SDL_ReleaseGPUShader(device, vertex); }
        if (fragment != nullptr) { SDL_ReleaseGPUShader(device, fragment); }
    }

    // HOW MUCH IS THE SHADER-CREATION CHECK WORTH? Measured rather than assumed:
    // hand the same call a module that is definitely not SPIR-V and see.
    {
        std::vector<std::uint32_t> rubbish{raster::spirv::magic, 0x00010000, 0,         4, 0,
                                           0xDEADBEEF,           0xDEADBEEF, 0xDEADBEEF};
        SDL_GPUShaderCreateInfo info{};
        info.code = reinterpret_cast<const Uint8 *>(rubbish.data());
        info.code_size = rubbish.size() * sizeof(std::uint32_t);
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        SDL_GPUShader * junk = SDL_CreateGPUShader(device, &info);
        if (junk != nullptr) {
            // It accepted nonsense, so acceptance proves nothing about validity.
            // PRINTED rather than left in a comment, because the worth of every
            // check above depends on this answer and a comment can go stale.
            //
            // Measured on BOTH drivers this engine has been run against: lavapipe
            // here, and a real Intel Arc through the Windows cross-build. Neither
            // rejects garbage at shader creation, and neither rejects a module
            // with mismatched OpFAdd operand widths at PIPELINE creation either -
            // which was tried by deliberately breaking the emitter's operand
            // broadcast and watching the pipeline build anyway.
            //
            // That is the correct mental model rather than a driver bug: Vulkan
            // is an explicit API and trusts its input. Validating SPIR-V is the
            // application's job, which is what tools/check-spirv.py is for.
            std::printf("     NOTE: this driver accepts invalid SPIR-V, so everything above\n");
            std::printf("           is a SMOKE TEST - the bytes reach the driver and survive\n");
            std::printf("           its compiler, which is worth knowing and is NOT the same\n");
            std::printf("           as being valid. Real validation is tools/check-spirv.py,\n");
            std::printf("           which needs spirv-val - not installed here.\n");
            SDL_ReleaseGPUShader(device, junk);
        } else {
            std::printf("     this driver rejects invalid SPIR-V, so the checks above are real\n");
        }
    }
    SDL_DestroyGPUDevice(device);
}

int main() {
    // The GPU backend needs SDL video up before a device can exist. `offscreen`
    // so this runs with no display; `dummy` has no Vulkan surface support and
    // is what a naive headless setup would pick.
    if (std::getenv("SDL_VIDEODRIVER") == nullptr) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SKIP all gpu tests (SDL video would not start: %s)\n", SDL_GetError());
        REPORT("gpu_basics");
    }

    test_fallback_when_the_gpu_is_refused();
    test_automatic_always_yields_something();
    test_force_gpu_reports_failure_rather_than_lying();
    test_environment_preference();

    test_gpu_matches_software_on_opaque_content();
    test_gpu_matches_software_on_translucent_content();
    test_gpu_scrolling_does_not_reraster();
    test_gpu_through_the_compositor_thread();
    test_a_driver_accepts_our_spirv();

    if (skipped > 0) {
        std::printf("     %d gpu test(s) skipped - no device on this machine\n", skipped);
    }
    SDL_Quit();
    REPORT("gpu_basics");
}
