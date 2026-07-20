// Space Invaders (John Pitchers, CC0) - a full Vite + BabylonJS ES-module
// game, bundled at build time (tools/js-bundle.py) into ONE script that
// ctbrowser parses BY VALUE at runtime, and run against babylon.hpp's
// software 3D renderer. The unmodified game HTML/JS lives in
// examples/space-invaders.html -> space-invaders.inc (raw-string literal).
//
// Upstream: https://github.com/johnpitchers/Space-Invaders (CC0).
// Regenerate the .inc:
//   python3 tools/js-bundle.py <game>/src/index.html -o /tmp/si.html
//   (trim head, size the canvas) then tools/html-to-inc.py.
//
// Build: make space-invaders   (needs SDL3). Arrow keys move,
// Shift/Space/Enter shoot, Enter starts.

#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <ctbrowser/embed.hpp>
#include <SDL3/SDL_main.h>
#include <span>

// std::embed opt-in: the #depend gate the patched compiler requires before its
// constant evaluator may read files - so assets.hpp can bake the @font-face
// PressStart2P-Regular.ttf (and any other examples/assets file the page names)
// straight into the binary. Other compilers skip the directive and fall back to
// runtime file loading.
#if defined(__has_builtin)
#	if __has_builtin(__builtin_std_embed)
#		pragma clang diagnostic push
#		pragma clang diagnostic ignored "-Wc++2d-extensions"
#depend "examples/assets/**"
#		pragma clang diagnostic pop
#	endif
#endif

using space_invaders = ctbrowser::page<
#include "space-invaders.inc"
>;

int main(int, char **) {
    ctbrowser::app_options opts;
    opts.width = 900;
    opts.height = 700;
    opts.logical_w = 900;
    opts.logical_h = 700;
#if defined(__has_builtin) && __has_builtin(__builtin_std_embed)
    // The game loads the alien/player/mothership models with
    // SceneLoader.ImportMeshAsync("", "/assets/models/", file, scene) - the URL is
    // built at runtime, so the auto-scanner can't see it; embed each GLB here and
    // register it under the exact "/assets/models/<file>" path the game requests.
    static constexpr std::span<const unsigned char> M_A1 = ctbrowser::embed<unsigned char>("examples/assets/models/Alien_1.glb");
    static constexpr std::span<const unsigned char> M_A2 = ctbrowser::embed<unsigned char>("examples/assets/models/Alien_2.glb");
    static constexpr std::span<const unsigned char> M_A3 = ctbrowser::embed<unsigned char>("examples/assets/models/Alien_3.glb");
    static constexpr std::span<const unsigned char> M_MS = ctbrowser::embed<unsigned char>("examples/assets/models/MotherShip.glb");
    static constexpr std::span<const unsigned char> M_P1 = ctbrowser::embed<unsigned char>("examples/assets/models/Player_1.glb");
    opts.assets.push_back({"/assets/models/Alien_1.glb", M_A1.data(), M_A1.size()});
    opts.assets.push_back({"/assets/models/Alien_2.glb", M_A2.data(), M_A2.size()});
    opts.assets.push_back({"/assets/models/Alien_3.glb", M_A3.data(), M_A3.size()});
    opts.assets.push_back({"/assets/models/MotherShip.glb", M_MS.data(), M_MS.size()});
    opts.assets.push_back({"/assets/models/Player_1.glb", M_P1.data(), M_P1.size()});
#endif
    return ctbrowser::run_app<space_invaders>(opts);
}
