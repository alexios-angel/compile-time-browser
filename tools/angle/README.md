# The ANGLE spike — stage 0 of `docs/angle-plan.md`

`spike.cpp` is the measurement that decided whether ANGLE is worth adopting: a
surfaceless EGL context, one full-screen triangle with a non-trivial fragment
shader, read back into memory the way a canvas would need.

**It is not built by this repository.** ANGLE is not a dependency; this is here
so the number can be reproduced and disputed.

```sh
# once: ~17 GB, about 25 minutes on 8 cores
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH=$PWD/depot_tools:$PATH
git clone https://github.com/alexios-angel/angle.git && cd angle
python3 scripts/bootstrap.py && gclient sync -D --no-history --shallow
gn gen out/Release --args='
  is_debug=false is_component_build=true treat_warnings_as_errors=false
  angle_enable_vulkan=true angle_enable_gl=false angle_enable_null=false
  angle_enable_wgpu=false angle_build_tests=false angle_build_all=false
  angle_use_wayland=false angle_use_x11=false'
ninja -C out/Release libEGL libGLESv2

g++ -std=c++17 -O2 -I include tools/angle/spike.cpp \
    -L out/Release -lEGL -lGLESv2 -Wl,-rpath,$PWD/out/Release -o spike
VK_ICD_FILENAMES=$PWD/out/Release/vk_swiftshader_icd.json ./spike
```

Three things had to be got right and each cost a run:

* **`treat_warnings_as_errors=false`.** ANGLE's own `FixedVector.h` trips its own
  `-Wunsafe-buffer-usage` under the bundled clang.
* **Leave `use_custom_libcxx` alone.** Forcing it off - to match this tree's
  libstdc++ - broke `Color.inc` on `std::strong_order`, and it was never needed:
  EGL and GLES are C APIs, so ANGLE's C++ library never crosses the boundary.
* **Ask for SwiftShader by name**, with `EGLint` attributes and not `EGLAttrib`
  ones. `eglGetPlatformDisplayEXT` takes 32-bit attributes; building the 64-bit
  array and casting it made every second word read as zero, and the only symptom
  was `EGL_NO_DISPLAY` with nothing logged.
