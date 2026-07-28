#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

// Input, described without reference to SDL.
//
// The engine takes THESE, and :app translates SDL events into them. That is
// what keeps the whole browser testable headlessly: a test drives clicks,
// scrolls and keys directly, with no window, no display and no event queue.
// the previous engine made the same split for the same reason.

namespace ctbrowser::shell {

enum class input_kind : std::uint8_t {
    mouse_move,
    mouse_down,
    mouse_up,
    wheel,
    key_down,
    key_up,     // a game holding a key needs to learn when it was RELEASED;
                // without this every held key sticks down forever
    text_input, // typed characters, not key codes - the only path that gets
                // IME, dead keys and non-Latin layouts right
    resize,
};

struct input_event {
    input_kind kind = input_kind::mouse_move;
    float x = 0;       // viewport pixels; for `resize`, the new width
    float y = 0;       // viewport pixels; for `resize`, the new height
    float wheel_y = 0; // notches, positive = away from the user
    // key_down/key_up: the DOM `code` of the physical key - "ArrowLeft",
    // "Space", "KeyA", "Digit1", "Enter". text_input: the typed text.
    //
    // This IS the DOM vocabulary, not a private one that has to be translated
    // at the boundary. The private one ("Left", "Return") was invisible to
    // pages, which compare against `e.code`.
    std::string key;
    // 0 is the left button. The DOM numbers the right one 2, and so does this -
    // there was no concept of it at all before, so a context menu had nothing
    // to open on.
    std::uint8_t button = 0;
    bool shift = false; // extends a selection rather than moving the caret
    bool ctrl = false;  // the clipboard and select-all shortcuts

    [[nodiscard]] static input_event mouse_move_to(float x, float y) {
        return input_event{input_kind::mouse_move, x, y, 0, {}, 0};
    }
    static constexpr std::uint8_t left_button = 0;
    static constexpr std::uint8_t right_button = 2;

    [[nodiscard]] static input_event mouse_down_at(float x, float y, std::uint8_t button = 0) {
        return input_event{input_kind::mouse_down, x, y, 0, {}, button};
    }
    [[nodiscard]] static input_event mouse_up_at(float x, float y, std::uint8_t button = 0) {
        return input_event{input_kind::mouse_up, x, y, 0, {}, button};
    }
    [[nodiscard]] static input_event wheel_by(float notches) {
        return input_event{input_kind::wheel, 0, 0, notches, {}, 0};
    }
    [[nodiscard]] static input_event key_press(std::string code, bool shift = false,
                                               bool ctrl = false) {
        return input_event{input_kind::key_down, 0, 0, 0, std::move(code), 0, shift, ctrl};
    }
    [[nodiscard]] static input_event key_release(std::string code, bool shift = false,
                                                 bool ctrl = false) {
        return input_event{input_kind::key_up, 0, 0, 0, std::move(code), 0, shift, ctrl};
    }
    [[nodiscard]] static input_event typed(std::string text) {
        return input_event{input_kind::text_input, 0, 0, 0, std::move(text), 0, false, false};
    }
    [[nodiscard]] static input_event resized(int width, int height) {
        return input_event{
            input_kind::resize, static_cast<float>(width), static_cast<float>(height), 0, {}, 0};
    }
};

// The DOM `key` for a `code`: what the key MEANS rather than which key it is.
// `code` is "KeyA" whatever the layout; `key` is "a", or "A" with shift.
// Pages read both, and a page reading `e.key` got nothing at all before.
[[nodiscard]] inline std::string dom_key_value(std::string_view code, bool shift) {
    if (code.size() == 4 && code.starts_with("Key")) {
        const char letter = code[3];
        return std::string{shift ? letter : static_cast<char>(letter - 'A' + 'a')};
    }
    if (code.size() == 6 && code.starts_with("Digit")) { return std::string{code[5]}; }
    if (code == "Space") { return " "; }
    if (code == "Minus") { return shift ? "_" : "-"; }
    if (code == "Equal") { return shift ? "+" : "="; }
    if (code == "Comma") { return shift ? "<" : ","; }
    if (code == "Period") { return shift ? ">" : "."; }
    if (code == "Slash") { return shift ? "?" : "/"; }
    // Everything else - the arrows, Enter, Escape, the function keys - has the
    // same spelling either way.
    return std::string{code};
}

} // namespace ctbrowser::shell
