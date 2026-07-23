// The interactive widget gallery: every form control with its Firefox
// chrome and default behavior - type into the fields (real caret),
// toggle the checkboxes, pick a radio, open the select, submit or
// reset the form, unfold the details. The status line narrates the
// events (input/change/submit) as they fire.
//
// Build: cmake --build --preset default --target ctbrowser-example-widgets

#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>widget gallery</title>
<style>
	h1 { font-size: 24px }
	.row { margin: 6px 0 }
	label { font-family: sans-serif; font-size: 14px }
	#status { background-color: #eef4ff; padding: 6px; font-family: monospace; font-size: 13px }
	button:hover { color: #003399 }
</style>
<h1>Widgets</h1>

<form id=f>
	<div class=row><label for=name>name</label> <input type=text id=name value=ada></div>
	<div class=row><label for=pw>password</label> <input type=password id=pw></div>
	<div class=row><textarea id=notes rows=3 cols=28>multi-line
notes live here</textarea></div>
	<div class=row>
		<label><input type=checkbox id=opt checked> option one</label>
		<label><input type=checkbox id=opt2> option two</label>
	</div>
	<div class=row>
		<label><input type=radio name=size id=s1 checked> small</label>
		<label><input type=radio name=size id=s2> large</label>
	</div>
	<div class=row><select id=color>
		<option value=red>red</option>
		<option value=green selected>green</option>
		<option value=blue>blue</option>
	</select></div>
	<div class=row>
		<button id=send>Submit</button>
		<button type=reset id=undo>Reset</button>
		<button id=off disabled>Disabled</button>
	</div>
</form>

<details id=more><summary>more settings</summary>
	<p>These unfold like Firefox's disclosure widget.</p>
</details>

<p><a href=https://example.com id=lnk>open example.com</a></p>

<p id=status>ready.</p>
<script>
	let status = getElementById("status");
	let f = getElementById("f");
	getElementById("name").oninput = function () {
		status.setText("name: " + getElementById("name").value);
	};
	getElementById("opt").onchange = function () {
		status.setText("option one: " + getElementById("opt").checked);
	};
	getElementById("s2").onchange = function () { status.setText("size: large"); };
	getElementById("color").onchange = function () {
		status.setText("color: " + getElementById("color").value);
	};
	f.onsubmit = function (e) {
		status.setText("submitted: " + getElementById("name").value +
		               " / " + getElementById("color").value);
	};
</script>)">;

int main(int, char **) {
	return ctbrowser::run_app<app>();
}
