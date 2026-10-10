//===- input.hpp - Platform-neutral input events ----------------*- C++ -*-===//
//
// Input, described without reference to SDL.
//
// The engine consumes these events, and the ctbrowser-app library (lib/App)
// translates SDL events into them. That keeps the whole browser testable
// headlessly: a test drives clicks, scrolls and keys directly, with no
// window, no display and no event queue.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

namespace ctbrowser::shell {

/// What an input_event reports.
enum class input_kind : std::uint8_t {
    mouse_move,
    mouse_down,
    mouse_up,
    wheel,
    key_down,
    key_up,     // A game holding a key needs to learn when it was released.
    text_input, // Typed characters, not key codes: the only path that gets
                // IME, dead keys and non-Latin layouts right.
    resize,
};

/// One input event, built with the named constructors below.
struct input_event {
    input_kind kind = input_kind::mouse_move;
    float x = 0;       // Viewport pixels; for `resize`, the new width.
    float y = 0;       // Viewport pixels; for `resize`, the new height.
    float wheel_y = 0; // Notches; positive is away from the user.
    /// key_down and key_up: the DOM `code` of the physical key ("ArrowLeft",
    /// "Space", "KeyA", "Digit1", "Enter"), verbatim, because pages compare
    /// against `e.code`. text_input: the typed text.
    std::string key;
    /// The mouse button, numbered as the DOM does: 0 is left, 2 is right.
    std::uint8_t button = 0;
    bool shift = false; // Extends a selection rather than moving the caret.
    bool ctrl = false;  // The clipboard and select-all shortcuts.
    /// Whether `x` and `y` mean anything. Only a wheel needs asking: pointer
    /// events always carry a position, and a wheel over a scrollable control
    /// needs one to scroll that control. Declared after the older members so
    /// existing aggregate initialisers keep compiling.
    bool has_pointer = false;
    float wheel_x = 0; // Notches; positive is to the right (SDL's sign and DOM deltaX's).

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
    /// A wheel with no pointer position. The page scrolls, since there is no
    /// way to tell what the notch was aimed at.
    [[nodiscard]] static input_event wheel_by(float notches) {
        return input_event{input_kind::wheel, 0, 0, notches, {}, 0};
    }
    /// A wheel at a point. \p x and \p y are where the pointer is, not a
    /// delta: a wheel over a scrollable control scrolls that control rather
    /// than the page.
    [[nodiscard]] static input_event wheel_at(float x, float y, float notches) {
        input_event out{input_kind::wheel, x, y, notches, {}, 0};
        out.has_pointer = true;
        return out;
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

/// The DOM `key` for a `code`: what the key means rather than which key it
/// is. `code` is "KeyA" whatever the layout; `key` is "a", or "A" with shift.
/// Assumes a US layout.
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
    // Everything else (the arrows, Enter, Escape, the function keys) has the
    // same spelling as its code.
    return std::string{code};
}

/// The legacy DOM `keyCode` (and `which`) for a `code`.
///
/// Deprecated, but many libraries depend on it; for example, Phaser matches
/// keys by number (`KeyCodes.LEFT` is 37). The values are the fixed table that
/// every browser reports. Returns 0 for codes without a legacy value.
[[nodiscard]] inline int dom_key_code(std::string_view code) {
    // Letters and digits are positional: "KeyA" is 65 whatever the layout says
    // the key produces.
    if (code.size() == 4 && code.starts_with("Key")) {
        return static_cast<int>(code[3]); // 'A' is 65, and so is KeyA.
    }
    if (code.size() == 6 && code.starts_with("Digit")) {
        return static_cast<int>(code[5]); // '0' is 48, and so is Digit0.
    }
    if (code.size() == 8 && code.starts_with("Numpad") && code[6] >= '0' && code[6] <= '9') {
        return 96 + (code[6] - '0');
    }
    if (code.size() >= 2 && code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        // F1 is 112. Parsed rather than tabled so F10-F12 come out right.
        int n = 0;
        for (std::size_t i = 1; i < code.size(); ++i) {
            if (code[i] < '0' || code[i] > '9') { return 0; }
            n = n * 10 + (code[i] - '0');
        }
        return n >= 1 && n <= 12 ? 111 + n : 0;
    }
    struct named {
        std::string_view code;
        int value;
    };
    static constexpr named table[] = {
        {"Backspace", 8},    {"Tab", 9},
        {"Enter", 13},       {"NumpadEnter", 13},
        {"ShiftLeft", 16},   {"ShiftRight", 16},
        {"ControlLeft", 17}, {"ControlRight", 17},
        {"AltLeft", 18},     {"AltRight", 18},
        {"Pause", 19},       {"CapsLock", 20},
        {"Escape", 27},      {"Space", 32},
        {"PageUp", 33},      {"PageDown", 34},
        {"End", 35},         {"Home", 36},
        {"ArrowLeft", 37},   {"ArrowUp", 38},
        {"ArrowRight", 39},  {"ArrowDown", 40},
        {"Insert", 45},      {"Delete", 46},
        {"MetaLeft", 91},    {"MetaRight", 92},
        {"NumLock", 144},    {"ScrollLock", 145},
        {"Semicolon", 186},  {"Equal", 187},
        {"Comma", 188},      {"Minus", 189},
        {"Period", 190},     {"Slash", 191},
        {"Backquote", 192},  {"BracketLeft", 219},
        {"Backslash", 220},  {"BracketRight", 221},
        {"Quote", 222},
    };
    for (const named & entry : table) {
        if (entry.code == code) { return entry.value; }
    }
    // 0 rather than a guess: a page testing `keyCode === 0` learns nothing,
    // and a page testing against a real code correctly fails to match.
    return 0;
}

} // namespace ctbrowser::shell
