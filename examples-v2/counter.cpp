// The hero demo: one HTML string is the whole application.
//
// Compare with v1's counter.cpp: two includes, a platform main shim, a `page<>`
// NTTP and two static_asserts. This is one import and one call - and the page is
// an ordinary runtime string, so it can come from a file, a socket or a text box
// just as easily.

import ctbrowser;

int main() {
	ctbrowser::app_options options;
	options.title = "counter";
	options.width = 480;
	options.height = 320;

	return ctbrowser::run_app(R"(<!DOCTYPE html>
<title>counter</title>
<style>
	body       { font-size: 16px; padding: 16px; background-color: #f6f7f9 }
	h1         { font-size: 32px; color: #222222 }
	#panel     { background-color: #ffffff; padding: 12px; width: 320px;
	             border-color: #c8ccd4; border-width: 1px }
	#count     { font-size: 48px; color: gray; padding: 8px }
	#count.hot { color: #ff8800 }
	.hint      { color: #888888 }
</style>
<h1>Clicks</h1>
<div id=panel>
	<p id=count>0</p>
	<p class=hint>click the number</p>
</div>
<script>
	var clicks = 0;
	document.getElementById("panel").addEventListener("click", function () {
		clicks = clicks + 1;
		var el = document.getElementById("count");
		el.setText(String(clicks));
		if (clicks >= 5) { el.addClass("hot"); }
	});
</script>)", options);
}
