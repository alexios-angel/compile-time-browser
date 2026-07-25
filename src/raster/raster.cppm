export module ctbrowser.raster;

// Raster and composition: display list in, pixels out.
//
//   surface     pixels as span + explicit stride (libstdc++ 13 has no mdspan)
//   tile        the unit of raster work, in CONTENT space so a scroll does not
//               invalidate it
//   backend     the RasterBackend concept - software and GPU are compile-time
//               interchangeable, and "did I implement all of it" is an error
//   software    the FIRST backend, so everything downstream is testable
//               headlessly and byte-for-byte before any GPU code exists
//   compositor  draw() for a full frame, recomposite() for a scroll

export import :surface;
export import :tile;
export import :backend;
export import :software;
export import :compositor;
