#include <ctbrowser/raster/ttf.hpp>

// ttf: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::raster {

bool ttf_backend::add_face(std::string family, bool bold, bool italic,
                           std::span<const std::byte> bytes) {
    if (!started_ || bytes.empty()) { return false; }
    const std::lock_guard guard{mutex_};
    // Opened once here purely to reject a file that is not a font; the
    // sized fonts the drawing path uses are opened on demand.
    loaded_face entry;
    entry.bytes.assign(bytes.begin(), bytes.end());
    TTF_Font * probe = open_sized(entry.bytes, 16);
    if (probe == nullptr) { return false; }
    TTF_CloseFont(probe);
    faces_[face_key{lowered(family), bold, italic}] = std::move(entry);
    return true;
}

std::size_t ttf_backend::face_count() const {
    const std::lock_guard guard{mutex_};
    return faces_.size();
}

bool ttf_backend::has_face(std::string_view family, bool bold, bool italic) const {
    const std::lock_guard guard{mutex_};
    return faces_.find(face_key{lowered(family), bold, italic}) != faces_.end();
}

void ttf_backend::set_default_family(std::string family) {
    const std::lock_guard guard{mutex_};
    default_family_ = lowered(family);
}

std::string ttf_backend::lowered(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

int ttf_backend::pixel_size(float font_size) noexcept {
    const int size = static_cast<int>(font_size + 0.5f);
    return size < 1 ? 1 : size;
}

TTF_Font * ttf_backend::open_sized(const std::vector<std::byte> & bytes, int size) {
    SDL_IOStream * source = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (source == nullptr) { return nullptr; }
    // closeio: the stream belongs to the font from here on.
    return TTF_OpenFontIO(source, true, static_cast<float>(size));
}

} // namespace ctbrowser::raster
