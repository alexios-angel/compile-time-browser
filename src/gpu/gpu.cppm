export module ctbrowser.gpu;

// Hardware-accelerated composition, and the fallback when there is none.
//
//   device   sdl_gpu_backend - SDL_GPUDevice, tile textures, one textured quad
//            per tile. Raster stays on the CPU and is SHARED with the software
//            backend, so the two produce comparable images by construction.
//   select   create_renderer() - try the GPU, fall back to software, and say
//            why. Every browser needs this; a machine with no usable GPU is
//            not an error case, it is a normal Tuesday.

export import :device;
export import :select;
