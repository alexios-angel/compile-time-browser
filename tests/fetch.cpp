// The web fetch() proof, headless: `const response = await fetch(url)`
// works in page scripts - async/await, promise chaining, response
// text()/json()/bytes(), and rejection for URLs that never got baked
// in. Tests inject the "fetched" bytes through the engine's embedded-
// asset seam (the same registry compile-time std::fetch fills on
// --fetch-allow builds), so no network is touched here.
#include <ctbrowser.hpp>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures; \
		} \
	} while (0)

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>fetch</title>
<body>
<div id="motd"></div>
<script>
	let got_status = 0;
	let got_ok = false;
	let speed = 0;
	let lives = 0;
	let tags = null;
	let motd = "";
	let chained = "";
	let missing_name = "";
	let missing_caught = false;
	let first_byte = -1;

	async function boot() {
		const response = await fetch("https://assets.test/config.json");
		got_status = response.status;
		got_ok = response.ok;
		const config = await response.json();
		speed = config.speed;
		lives = config.lives;
		tags = config.tags;

		const motd_response = await fetch("https://assets.test/motd.txt");
		motd = await motd_response.text();
		document.getElementById("motd").setText(motd);

		const raw = await (await fetch("https://assets.test/motd.txt")).bytes();
		first_byte = raw[0];

		try {
			await fetch("https://assets.test/never-baked.bin");
		} catch (e) {
			missing_caught = true;
			missing_name = e.name;
		}
	}
	boot();

	// .then chaining works too - same settled promises, no await sugar
	fetch("https://assets.test/motd.txt")
		.then((r) => r.text())
		.then((t) => { chained = t.toUpperCase(); });
</script>
)">;

int main() {
	static const char config_json[] = R"({"speed": 7, "lives": 3, "tags": ["fast", "fun"]})";
	static const char motd_txt[] = "hello from the build";
	std::vector<ctbrowser::embedded_asset> baked;
	baked.push_back({"https://assets.test/config.json",
	                 reinterpret_cast<const unsigned char *>(config_json),
	                 std::strlen(config_json)});
	baked.push_back({"https://assets.test/motd.txt",
	                 reinterpret_cast<const unsigned char *>(motd_txt),
	                 std::strlen(motd_txt)});

	ctbrowser::engine<app> e({}, {}, std::move(baked));
	CHECK(e.script.ok());

	// the async flow ran to completion synchronously (settled promises)
	CHECK(e.script["got_status"].to<int>() == 200);
	CHECK(e.script["got_ok"].to<bool>());
	CHECK(e.script["speed"].to<int>() == 7);
	CHECK(e.script["lives"].to<int>() == 3);
	CHECK(e.script["tags"][0].to<std::string>() == "fast");
	CHECK(e.script["tags"][1].to<std::string>() == "fun");
	CHECK(e.script["motd"].to<std::string>() == "hello from the build");
	CHECK(e.script["first_byte"].to<int>() == 'h');

	// the DOM saw the fetched data
	ctbrowser::node * motd = e.doc.by_id("motd");
	CHECK(motd != nullptr && motd->text == "hello from the build");

	// missing URL = network-failure shaped rejection, catchable
	CHECK(e.script["missing_caught"].to<bool>());
	CHECK(e.script["missing_name"].to<std::string>() == "TypeError");

	// .then chaining
	CHECK(e.script["chained"].to<std::string>() == "HELLO FROM THE BUILD");

	if (failures == 0) { std::printf("fetch suite: all checks passed\n"); }
	return failures;
}
