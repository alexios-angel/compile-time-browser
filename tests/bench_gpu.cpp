// Composite cost: GPU against software, on the frame a scroll actually runs.
//
// Stage 5 ended with the honest observation that what remained in the scroll
// column was the software blit itself - about a megapixel copied per frame.
// This is the measurement of whether moving that to the GPU was worth doing.
//
// The comparison is deliberately narrow. RASTER IS THE SAME CODE on both sides
// (ctbrowser.raster's :draw), so the only thing that differs is composition:
// the software backend blends tiles into a target buffer with the CPU, the GPU
// backend uploads them once and draws a textured quad each. A scroll re-runs
// only the second half, which is why that is the number in the table.
//
// Where there is no device this prints what it can and says the rest is
// unavailable, rather than reporting zeros.

#include <ctbrowser/core/core.hpp>
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;
import ctbrowser.paint;
import ctbrowser.raster;
import ctbrowser.gpu;

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>

using clock_type = std::chrono::steady_clock;
using namespace ctbrowser;

namespace {

[[nodiscard]] std::string build_html(int rows) {
    std::string out = "<body>";
    for (int r = 0; r < rows; ++r) {
        out += "<div class=row>Row " + std::to_string(r) +
               " with enough words in it that the line breaker has real work to do</div>";
    }
    out += "</body>";
    return out;
}

constexpr std::string_view sheet =
    "body { margin: 0; padding: 0; background-color: #ffffff }"
    ".row { display: block; margin: 1px; padding: 2px; font-size: 8px; color: #202020; "
    "background-color: #eef2f8 }";

template <typename F> [[nodiscard]] double time_ms(int reps, F && f) {
    const auto start = clock_type::now();
    for (int i = 0; i < reps; ++i) { f(); }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / reps;
}

struct page {
    atom_table atoms;
    document doc{atoms};
    style::engine styles{atoms};
    style::style_map resolved;
    layout::box_node boxes;
    layout::fragment placed;
    paint::layer_tree layers;

    void load(int rows, float viewport) {
        (void)parse_html(doc, build_html(rows));
        styles.add_sheet(sheet, 1);
        const auto txn = doc.read();
        resolved = styles.resolve_all(txn);
        layout::box_builder builder{atoms, resolved};
        boxes = builder.build(txn, txn.root());
        const layout::engine eng{raster::font8x8_advance};
        placed = eng.run(boxes, viewport);
        const paint::recorder rec{atoms};
        layers = rec.record_layers(placed);
    }
};

// A scroll frame: raster whatever came into view, composite, present. On a page
// already visited this is composition and nothing else, which is the case worth
// measuring - it is what every frame of a flick costs.
template <typename B>
[[nodiscard]] double scroll_frame_ms(B & backend, page & p, const rect & viewport, int steps) {
    (void)raster::draw(backend, p.layers, nullptr, raster::default_tile_extent, viewport);
    float at = 0;
    return time_ms(steps, [&] {
        at += 8; // a fraction of a tile, so almost nothing new is rastered
        p.layers.scroll_to(0, at);
        (void)raster::draw(backend, p.layers, nullptr, raster::default_tile_extent, viewport);
    });
}

void run_case(int rows, int w, int h) {
    page p;
    p.load(rows, static_cast<float>(w));
    const rect viewport{0, 0, static_cast<float>(w), static_cast<float>(h)};

    raster::software_backend cpu{w, h};
    const double cpu_first = time_ms(5, [&] {
        raster::software_backend fresh{w, h};
        (void)raster::draw(fresh, p.layers, nullptr, raster::default_tile_extent, viewport);
    });
    const double cpu_scroll = scroll_frame_ms(cpu, p, viewport, 60);

    // ONE device at a time. Holding several open at once crashed lavapipe here,
    // and nothing in the pipeline wants more than one anyway - the compositor
    // thread owns exactly one device for the life of the window.
    double gpu_first = 0;
    {
        auto fresh = gpu::sdl_gpu_backend::create(w, h);
        if (!fresh) {
            std::printf("%5d %6dx%-5d %9.3f %9.3f %11s %11s %8s\n", rows, w, h, cpu_first,
                        cpu_scroll, "-", "-", "no device");
            return;
        }
        gpu_first = time_ms(3, [&] {
            fresh->discard();
            (void)raster::draw(*fresh, p.layers, nullptr, raster::default_tile_extent, viewport);
        });
    }
    double gpu_scroll = 0;
    {
        auto device = gpu::sdl_gpu_backend::create(w, h);
        if (!device) { return; }
        p.layers.scroll_to(0, 0);
        gpu_scroll = scroll_frame_ms(*device, p, viewport, 60);
    }

    std::printf("%5d %6dx%-5d %9.3f %9.3f %11.3f %11.3f %7.2fx\n", rows, w, h, cpu_first,
                cpu_scroll, gpu_first, gpu_scroll, cpu_scroll / gpu_scroll);
}

} // namespace

int main() {
    if (std::getenv("SDL_VIDEODRIVER") == nullptr) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL video would not start: %s\n", SDL_GetError());
        return 0;
    }
    bool software_adapter = false;
    {
        auto probe = gpu::sdl_gpu_backend::create(16, 16);
        if (!probe) {
            std::printf("composite cost: NO GPU DEVICE on this machine - software column only\n\n");
        } else {
            software_adapter = probe->adapter_is_software();
            std::printf("composite cost, software vs GPU\n  driver:  %s\n  adapter: %s\n\n",
                        probe->driver().c_str(), probe->adapter().c_str());
        }
    }
    if (software_adapter) {
        // The single most important line this benchmark can print. Under WSL2
        // without GPU passthrough, in containers, and on most CI images, the
        // only Vulkan driver is lavapipe - a CPU implementation. The GPU path
        // runs and is CORRECT there, which is worth having, but the numbers
        // below are two CPU implementations racing each other and say nothing
        // whatsoever about hardware.
        std::printf("*** THIS ADAPTER IS A SOFTWARE IMPLEMENTATION OF VULKAN. ***\n"
                    "The GPU columns below measure the API path on a CPU, NOT hardware.\n"
                    "A real number needs a machine whose Vulkan/D3D12 driver reaches a GPU -\n"
                    "under WSL2 that means building the Windows .exe (cmake --preset windows)\n"
                    "and running it from Windows, since Linux binaries here see no adapter.\n\n");
    }
    std::printf("%5s %12s %9s %9s %11s %11s %8s\n", "rows", "viewport", "cpu 1st", "cpu scr",
                "gpu 1st", "gpu scr", "speedup");
    std::printf("%s\n", std::string(74, '-').c_str());
    run_case(40, 800, 600);
    run_case(200, 800, 600);
    run_case(800, 1280, 800);
    run_case(2000, 1920, 1080);
    std::printf("\nRaster is the SAME code on both sides, so the difference is composition\n"
                "alone. cpu scr / gpu scr is the number a scroll actually feels.\n");
    SDL_Quit();
    return 0;
}
