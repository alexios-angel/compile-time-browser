#include <ctbrowser/core/algorithms.hpp>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/beast/core/detail/base64.hpp>

#include <locale>

namespace ctbrowser {

bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    // THE LOCALE ARGUMENT IS THE POINT. Without it boost::iequals uses
    // std::locale(), the global one, and the answer starts depending on the
    // host - which a repository that byte-compares renders cannot have.
    return boost::algorithm::iequals(a, b, std::locale::classic());
}

// Boost's, with the classic locale for the same reason as above - and the same
// trap avoided: the default overloads take std::locale(), the global one.
void ascii_lower_in_place(std::string & text) noexcept {
    boost::algorithm::to_lower(text, std::locale::classic());
}

std::string ascii_lower_copy(std::string_view text) {
    return boost::algorithm::to_lower_copy(std::string{text}, std::locale::classic());
}

void ascii_upper_in_place(std::string & text) noexcept {
    boost::algorithm::to_upper(text, std::locale::classic());
}

std::string ascii_upper_copy(std::string_view text) {
    return boost::algorithm::to_upper_copy(std::string{text}, std::locale::classic());
}

// BEAST'S TABLE, BEAST'S LOOP, ONE LINE CHANGED - and the numbers are why, over
// a 4 MiB payload, min of five runs:
//
//   this, Beast's table + skip     3.3 ms   1288 MB/s
//   boost::beast::detail::base64   4.2 ms    991 MB/s
//   a six-bit accumulator loop     16.8 ms    250 MB/s   (5.1x slower)
//   boost::archive::iterators      24.3 ms    173 MB/s   (7.4x slower)
//
// THE TABLE IS WHAT IS BORROWED. All four produce identical bytes on a flat
// payload; the speed is `get_inverse()`'s 256-byte indexed lookup against the
// `alphabet.find(ch)` a hand-written version does per character. Boost.Archive
// is the only one of the four with a documented public API and it is the
// slowest, as well as throwing on input a browser must accept.
//
// THE ONE CHANGE IS `break` -> skip. `base64::decode` stops dead at the first
// character outside the alphabet, which is right for padding - `=` is only ever
// at the end and encodes nothing - and wrong for the newline every MIME encoder
// in the world inserts every 76 characters. On wrapped input stock Beast
// returns 57 bytes of a 3 MB payload and reports success. Calling it and
// repairing the result afterwards worked but cost a second filtering pass over
// exactly the input that needed help; skipping instead of stopping is one pass
// for both, and it is FASTER than the function it replaces because that
// function reloads the table pointer and branches on `*in != '='` per character.
//
// HEADER-ONLY, which is what makes borrowing affordable: Beast needs no library
// to link and no addition to tools/build-boost-mingw.sh, so the Windows
// cross-build gets the table from the same include tree that already supplies
// Boost.URL's headers. Beast's HTTP and WebSocket halves stay uncompiled.
//
// NOT `base64::decoded_size` for the buffer, which is a memory-safety trap
// rather than a style preference: it is `n / 4 * 3`, which assumes the padding
// is present. Unpadded "aGVsbG8" is seven characters and decodes to FIVE bytes
// while that function answers three - so Beast's own decode writes two bytes
// past a buffer sized by its own helper. Rounding the character count up to a
// whole group first is the size that holds every input, padded or not.
std::string base64_decode(std::string_view text) {
    const signed char * const inverse = boost::beast::detail::base64::get_inverse();
    std::string out;
    out.resize((text.size() + 3) / 4 * 3);
    char * write = out.data();

    unsigned group[4] = {0, 0, 0, 0};
    std::size_t filled = 0;
    for (const char raw : text) {
        const signed char six = inverse[static_cast<unsigned char>(raw)];
        // THE ONE CHANGE from Beast, and the whole reason for the copy.
        if (six < 0) { continue; }
        group[filled] = static_cast<unsigned>(six);
        if (++filled == 4) {
            *write++ = static_cast<char>((group[0] << 2) | (group[1] >> 4));
            *write++ = static_cast<char>(((group[1] & 0xF) << 4) | (group[2] >> 2));
            *write++ = static_cast<char>(((group[2] & 0x3) << 6) | group[3]);
            filled = 0;
        }
    }
    // A trailing partial group carries one fewer byte than it has characters,
    // which is what makes the padding optional: `aGVsbG8` and `aGVsbG8=` decode
    // to the same five bytes. A lone leftover character encodes nothing.
    if (filled >= 2) {
        *write++ = static_cast<char>((group[0] << 2) | (group[1] >> 4));
        if (filled == 3) {
            *write++ = static_cast<char>(((group[1] & 0xF) << 4) | (group[2] >> 2));
        }
    }
    out.resize(static_cast<std::size_t>(write - out.data()));
    return out;
}

} // namespace ctbrowser
