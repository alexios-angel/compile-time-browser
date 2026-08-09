#include <ctbrowser/shell/page/svg_cache.hpp>

#include <algorithm>
#include <boost/container_hash/hash.hpp>
#include <cctype>
#include <charconv>
#include <utility>

#include <ctbrowser/raster/svg.hpp>

namespace ctbrowser::shell {

namespace {

// `boost::hash`, not a hand-rolled FNV-1a: FNV walks the bytes ONE AT A TIME
// and this hashes whole SVG documents. Same argument core/containers.hpp makes
// for preferring it to std::hash, and this is the larger input of the two.
//
// The value is an in-memory cache token only - `set_source` compares the source
// in FULL on a hash hit (see below), nothing persists it, and no golden depends
// on it - so changing the function changes no observable behaviour.
[[nodiscard]] std::uint64_t content_hash(std::string_view text) noexcept {
    return boost::hash<std::string_view>{}(text);
}

[[nodiscard]] bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

// The value of `name` in the FIRST tag of the document, which for a well-formed
// SVG is the root <svg>. Deliberately not a parser: this reads three attributes
// off one tag to get a box size, and a document whose root tag is malformed
// enough to defeat this is one plutosvg will refuse anyway.
[[nodiscard]] std::string_view attribute_in_root(std::string_view source, std::string_view name) {
    const std::size_t open = source.find("<svg");
    if (open == std::string_view::npos) { return {}; }
    // Bounded to the root tag so a `width` on a child <rect> cannot be mistaken
    // for the document's own.
    const std::size_t close = source.find('>', open);
    const std::string_view tag = source.substr(
        open, close == std::string_view::npos ? std::string_view::npos : close - open);

    for (std::size_t at = 0; (at = tag.find(name, at)) != std::string_view::npos;
         at += name.size()) {
        // A name match has to be a whole attribute name, or `width` matches
        // inside `stroke-width` and a stroke decides the document's size.
        if (at == 0 || !(is_space(tag[at - 1]))) { continue; }
        std::size_t after = at + name.size();
        while (after < tag.size() && is_space(tag[after])) { ++after; }
        if (after >= tag.size() || tag[after] != '=') { continue; }
        ++after;
        while (after < tag.size() && is_space(tag[after])) { ++after; }
        if (after >= tag.size()) { return {}; }
        const char quote = tag[after];
        if (quote != '"' && quote != '\'') { return {}; } // unquoted: not worth it
        const std::size_t begin = after + 1;
        const std::size_t end = tag.find(quote, begin);
        if (end == std::string_view::npos) { return {}; }
        return tag.substr(begin, end - begin);
    }
    return {};
}

// A CSS length that is an absolute number of pixels, or 0 for anything else.
// PERCENTAGES DELIBERATELY GIVE 0 - see the header: `width="100%"` is not a
// natural size, and reading the 100 as pixels turns a full-width banner into a
// stamp.
[[nodiscard]] float absolute_length(std::string_view text) {
    while (!text.empty() && is_space(text.front())) { text.remove_prefix(1); }
    while (!text.empty() && is_space(text.back())) { text.remove_suffix(1); }
    if (text.empty()) { return 0; }

    float value = 0;
    const auto [stop, err] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (err != std::errc{} || value <= 0) { return 0; }

    std::string_view unit = text.substr(static_cast<std::size_t>(stop - text.data()));
    while (!unit.empty() && is_space(unit.front())) { unit.remove_prefix(1); }
    if (unit.empty() || unit == "px") { return value; }
    return 0; // %, em, and everything else needs a context this does not have
}

} // namespace

svg_natural scan_svg_natural(std::string_view source) {
    svg_natural out;
    out.width = absolute_length(attribute_in_root(source, "width"));
    out.height = absolute_length(attribute_in_root(source, "height"));
    if (out.known()) { return out; }

    // Fall back to the viewBox, which carries the ASPECT even when the size is
    // a percentage or absent. Four numbers; the last two are the extent.
    //
    // `viewBox` with its capital B: this reads the ORIGINAL source, never the
    // DOM's lowercased copy, which is the reason the source is kept verbatim.
    const std::string_view box = attribute_in_root(source, "viewBox");
    float numbers[4]{};
    std::size_t found = 0;
    const char * at = box.data();
    const char * stop = box.data() + box.size();
    while (found < 4 && at < stop) {
        while (at < stop && (is_space(*at) || *at == ',')) { ++at; }
        const auto [next, err] = std::from_chars(at, stop, numbers[found]);
        if (err != std::errc{}) { break; }
        at = next;
        ++found;
    }
    if (found == 4 && numbers[2] > 0 && numbers[3] > 0) {
        // One axis may still have come from an absolute attribute above; the
        // viewBox only fills what is missing, so a `width="200"` with a square
        // viewBox gives a 200x200 rather than discarding the 200.
        if (out.width > 0 && out.height <= 0) {
            out.height = out.width * (numbers[3] / numbers[2]);
        } else if (out.height > 0 && out.width <= 0) {
            out.width = out.height * (numbers[2] / numbers[3]);
        } else if (out.width <= 0 && out.height <= 0) {
            out.width = numbers[2];
            out.height = numbers[3];
        }
    }
    return out;
}

std::size_t svg_store::raster_key_hash::operator()(const raster_key & k) const noexcept {
    // This was `0x9e3779b9...`, the golden-ratio mix, open-coded twice - which
    // is `boost::hash_combine` retyped from memory. Boost is already a
    // dependency of every target here.
    std::size_t hash = 0;
    boost::hash_combine(hash, k.content);
    boost::hash_combine(hash, k.width);
    boost::hash_combine(hash, k.height);
    return hash;
}

void svg_store::set_source(node_id id, std::string source) {
    if (source.empty()) { return; }
    const std::uint64_t content = content_hash(source);

    // Compare in full on a hash hit. A collision here would hand one graphic's
    // pixels to another element, which is a wrong picture rather than a crash
    // and would be very hard to see; the compare only runs when the hash
    // already matched, so it costs nothing in the common case.
    auto found = sources_.find(content);
    if (found == sources_.end()) {
        found = sources_.emplace(content, std::move(source)).first;
    } else if (found->second != source) {
        return; // a real collision: refuse rather than draw the wrong thing
    }

    entry e;
    e.content = content;
    e.natural = scan_svg_natural(found->second);
    for (auto & [at, existing] : by_node_) {
        if (at == id) {
            existing = e;
            return;
        }
    }
    by_node_.emplace_back(id, e);
}

const svg_store::entry * svg_store::find(node_id id) const noexcept {
    for (const auto & [at, e] : by_node_) {
        if (at == id) { return &e; }
    }
    return nullptr;
}

bool svg_store::has(node_id id) const noexcept {
    return find(id) != nullptr;
}

svg_natural svg_store::natural_of(node_id id) const {
    const entry * e = find(id);
    return e ? e->natural : svg_natural{};
}

std::shared_ptr<const paint::bitmap> svg_store::pixels_for(node_id id, int width, int height) {
    const entry * e = find(id);
    if (e == nullptr || width <= 0 || height <= 0) { return nullptr; }

    const raster_key key{e->content, width, height};
    if (const auto found = rasters_.find(key); found != rasters_.end()) {
        found->second.touched = true;
        return found->second.pixels;
    }

    const auto source = sources_.find(e->content);
    if (source == sources_.end()) { return nullptr; }

    paint::bitmap pixels = raster::render_svg(source->second, width, height);
    // Cached even when empty. A document plutosvg cannot render fails the same
    // way every frame, and without this the engine would retry the parse on
    // every single one.
    auto shared = std::make_shared<const paint::bitmap>(std::move(pixels));
    rasters_.emplace(key, raster_entry{shared, true});
    return shared->empty() ? nullptr : shared;
}

void svg_store::begin_frame() noexcept {
    for (auto & [key, value] : rasters_) { value.touched = false; }
}

void svg_store::end_frame() {
    std::erase_if(rasters_, [](const auto & item) { return !item.second.touched; });
}

void svg_store::clear() noexcept {
    by_node_.clear();
    sources_.clear();
    rasters_.clear();
}

} // namespace ctbrowser::shell
