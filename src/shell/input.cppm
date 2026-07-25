module;
#include <cstdint>
#include <string>
#include <string_view>

export module ctbrowser.shell:input;

import ctbrowser.core;

// Input, described without reference to SDL.
//
// The engine takes THESE, and :app translates SDL events into them. That is
// what keeps the whole browser testable headlessly: a test drives clicks,
// scrolls and keys directly, with no window, no display and no event queue.
// v1 made the same split for the same reason.

export namespace ctbrowser::shell {

enum class input_kind : std::uint8_t {
	mouse_move,
	mouse_down,
	mouse_up,
	wheel,
	key_down,
	text_input, // typed characters, not key codes - the only path that gets
	            // IME, dead keys and non-Latin layouts right
	resize,
};

struct input_event {
	input_kind kind = input_kind::mouse_move;
	float x = 0; // viewport pixels; for `resize`, the new width
	float y = 0; // viewport pixels; for `resize`, the new height
	float wheel_y = 0;   // notches, positive = away from the user
	std::string key;     // key_down: a DOM-ish name ("ArrowDown", "Home", ...)
	std::uint8_t button = 0;
	bool shift = false; // extends a selection rather than moving the caret

	[[nodiscard]] static input_event mouse_move_to(float x, float y) {
		return input_event{input_kind::mouse_move, x, y, 0, {}, 0};
	}
	[[nodiscard]] static input_event mouse_down_at(float x, float y, std::uint8_t button = 0) {
		return input_event{input_kind::mouse_down, x, y, 0, {}, button};
	}
	[[nodiscard]] static input_event mouse_up_at(float x, float y, std::uint8_t button = 0) {
		return input_event{input_kind::mouse_up, x, y, 0, {}, button};
	}
	[[nodiscard]] static input_event wheel_by(float notches) {
		return input_event{input_kind::wheel, 0, 0, notches, {}, 0};
	}
	[[nodiscard]] static input_event key_press(std::string name, bool shift = false) {
		return input_event{input_kind::key_down, 0, 0, 0, std::move(name), 0, shift};
	}
	[[nodiscard]] static input_event typed(std::string text) {
		return input_event{input_kind::text_input, 0, 0, 0, std::move(text), 0, false};
	}
	[[nodiscard]] static input_event resized(int width, int height) {
		return input_event{input_kind::resize, static_cast<float>(width),
		                   static_cast<float>(height), 0, {}, 0};
	}
};

} // namespace ctbrowser::shell
