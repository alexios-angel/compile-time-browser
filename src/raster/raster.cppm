export module ctbrowser.raster;

// Raster and composition: display list in, pixels out.
//
//   surface     pixels as span + explicit stride (libstdc++ 13 has no mdspan)
//   tile        the unit of raster work, in CONTENT space so a scroll does not
//               invalidate it
//   draw        display list -> pixels, shared by EVERY backend, so the GPU
//               image and the software image are comparable by construction
//   backend     the RasterBackend concept - software and GPU are compile-time
//               interchangeable, and "did I implement all of it" is an error
//   software    the FIRST backend, so everything downstream is testable
//               headlessly and byte-for-byte before any GPU code exists
//   renderer    the RUNTIME seam - a type-erased backend, so the engine can
//               fall back to software when there is no GPU and still use one
//               interface everywhere
//   compositor  draw() for a full frame, recomposite() for a scroll
//   pipeline    the compositor THREAD - one thread owns the device, raster
//               workers reach it over per-worker lock-free channels

export import :surface;
export import :draw;
export import :ttf;
export import :tile;
export import :backend;
export import :software;
export import :renderer;
export import :compositor;
export import :pipeline;
