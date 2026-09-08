// browser - the page's scripts: every classic <script> as its own program,
// ES modules in two passes over the graph, script images, and the natives and
// module loader the embedder side installs on the context.
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

void browser::run_scripts() {
    // Order matters on teardown too: the old context must go before the old
    // program it was executing.
    script_.reset();
    classic_programs_.clear();
    // AND THE PREVIOUS PAGE'S MODULES, which nothing cleared. Every
    // <script type="module"> is kept as its own program because its functions
    // close over its top-level frame, and the vector holding them was appended
    // to on every load and emptied on none - so a page that reloads ten times
    // held ten pages' worth of dead programs, each up to megabytes, reachable
    // only by this vector. The module registry itself lives in the context and
    // therefore DID go, which is what kept this a leak rather than a
    // correctness bug: nothing could still reach these to run them.
    module_programs_.clear();
    script_ = std::make_unique<script::context>();
    // The standard library goes in FIRST, so a page's own globals can
    // shadow it rather than the other way round.
    script::install_builtins(*script_);
    // WHAT TIME THE PAGE THINKS IT IS: a fixed base plus how long this page has
    // been running, so `Date.now()` advances, reads as a plausible instant, and
    // is still identical on every run - `tick()` moves the page clock by exactly
    // what it was given. An embedder that wants real time calls set_clock; the
    // SDL app does. See context::set_clock for why the frozen epoch had to go.
    script_->set_clock([this] {
        return clock_ ? clock_() : script::context::fixed_epoch_base + bindings_->now_ms();
    });
    canvases_.clear();
    forms_.clear();
    focused_ = node_id{};
    // The in-flight drag goes with the document. These are HANDLES into a slab
    // that has just been rebuilt, and forms_.state_of would happily seed fresh
    // state for a stale one - a page that reloads mid-drag would otherwise keep
    // auto-scrolling a field that no longer exists.
    field_selecting_ = node_id{};
    pressed_ = node_id{};
    selecting_ = false;
    // A SCRIPT MUTATION INVALIDATES THE CASCADE, and this said `paint` - so a
    // page that set an attribute, added a class or appended an element got its
    // display list re-recorded from the styles resolved at load. It was not
    // visible on the corpora because nothing there changes a style from script
    // and then looks: p5 and Phaser draw into a canvas, which invalidates
    // through canvases_.total_revision() instead.
    //
    // getComputedStyle is what made it visible. `dirty_` is the record of what a
    // frame has to re-run, and the flush below reads it to decide what to run
    // NOW - so a level that undersells the damage does not merely defer the
    // repaint, it makes the flush a no-op and the read answers from before the
    // write. `styles` is the honest level: the only callers are script mutating
    // the document, and every one of them can change which rules match.
    bindings_ = std::make_unique<dom_bindings>(
        *doc_, atoms_, canvases_, forms_, [this] { mark(dirty::styles); },
        [this](node_id id) { (void)focus(id); });
    // The back end a caller chose before the page loaded - see
    // browser::prefer_angle_webgl. Applied here because this is the first
    // moment the object that owns WebGL contexts exists.
    // THE CASCADE'S ENGINE, so `querySelector` runs the matcher a stylesheet runs.
    // reset_document() replaces it on every load and this object is rebuilt after
    // that, so handing it over here is enough - the second call in reset_document
    // covers a reload that keeps these bindings.
    bindings_->observe_style_engine(*styles_);
    bindings_->prefer_angle(prefer_angle_webgl_);
    bindings_->observe_viewport(layout_viewport_width(), options_.height);
    bindings_->observe_resources(assets_, images_);
    bindings_->allow_network(network_allowed_);
    // `element.click()` performs the default action, which is the browser's half.
    bindings_->set_activate_hook([this](node_id id) { activate(id); });
    bindings_->set_alert_hook([this](const std::string & message) {
        alerts_.push_back(message);
        if (alert_hook_) { alert_hook_(message); }
    });
    bindings_->observe_location(location_href_, location_hash_);
    bindings_->install(*script_);
    // getComputedStyle ANSWERS ABOUT THE PAGE AS THE SCRIPT JUST LEFT IT.
    //
    // The object dom_bindings builds is made out of `resolved_`, `boxes_` and
    // `fragments_`, and all three are handed over by run_layout - which has not
    // run when a page's own <script> executes, because load_one_page marks the
    // document dirty and runs the scripts BEFORE the first frame. So every
    // getComputedStyle at load time read three null pointers, and every one
    // after a write to `el.style` read the cascade from before that write.
    // docs/css-conformance.md §5 measured what that costs: a throwaway patch
    // that published the camelCase spellings moved not one WPT subtest, because
    // the property was not on the object under any spelling.
    //
    // WRAPPED HERE RATHER THAN FLUSHED INSIDE THE BINDING. The pipeline is the
    // browser's and the bindings deliberately do not know that layout exists -
    // "a native that changes the document calls on_mutation, and the browser
    // decides what that invalidates", at the top of shell/bindings.hpp. This is
    // the same shadowing install_embedder_natives does two lines down, so an
    // embedder that defines its own `getComputedStyle` still wins.
    //
    // ONLY WHAT IS ACTUALLY STALE RUNS: a second call with nothing written in
    // between does no work at all, which is what keeps a page that reads one
    // property per element off a hundred relayouts. The level is left at `paint`
    // afterwards because the display list still has to be re-recorded before
    // anything is drawn - the flush answers a question, it does not make a frame.
    if (const script::value inner = script_->global("getComputedStyle"); inner.is_callable()) {
        auto * flushing = script_->allocate<script::native_object>(
            "getComputedStyle", [this, inner](script::context & c, std::span<script::value> args) {
                if (dirty_ >= dirty::styles) { resolve_styles(); }
                if (dirty_ >= dirty::layout) { run_layout(); }
                if (dirty_ > dirty::paint) { dirty_ = dirty::paint; }
                return c.call(inner, args);
            });
        // `inner` LIVES IN A C++ CAPTURE, which the precise collector cannot
        // see, and the global that held it has just been overwritten - so this
        // is the only reference left to it. See script::native_object::retained,
        // which exists for exactly this and says why a property would be worse.
        flushing->retained.push_back(inner);
        script_->define_global("getComputedStyle", script::value::object(flushing));
    }
    install_embedder_natives();
    script_error_.clear();

    // ONE ENTRY PER CLASSIC <script>, NOT ONE STRING FOR THE PAGE. Gluing them
    // together made the page's compiled form one artefact, so a two-line sketch
    // and the 4.5 MB library beside it shared a cache key and an edit to either
    // threw away both. It also made the page's scripts one PROGRAM, which is a
    // deviation the split fixes on the way past: per the HTML specification each
    // classic <script> is its own Script Record, so a parse error or an uncaught
    // throw in one does not stop the next. Measured before and after - the first
    // of two scripts failing to parse used to silence the second, and does not
    // now.
    //
    // What it costs is the one thing concatenation bought: a call in an EARLIER
    // script to a function declared in a LATER one. Chrome makes that a
    // ReferenceError; this engine used to make it work.
    std::vector<std::string> classic_scripts;
    // MODULES ARE COLLECTED SEPARATELY AND RUN SEPARATELY, because that is the
    // one thing they cannot share with a classic script: its top level is the
    // global scope and theirs is not. Concatenating them all - which is what
    // this did, and what makes the classic path cheap and correct - would put
    // every module's declarations on the global object and let them overwrite
    // each other. See docs/plans/modules.md.
    // WITH A SPECIFIER EACH, because the specifier is the registry key and two
    // module scripts sharing one means the second is taken for a module already
    // loaded and never runs at all. It also decides what `./dep.js` INSIDE the
    // script means: a `src`'d module resolves against its own URL, an inline
    // one against the page.
    std::vector<std::pair<std::string, std::string>> modules;
    {
        const auto txn = doc_->read();
        const atom script_tag = atoms_.intern_lower("script");
        const atom type_attribute = atoms_.intern("type");
        const auto walk = [&](auto && self, node_id at) -> void {
            // HTML <script> ONLY, for the same reason as <style> and <title>
            // above: SVG has a <script> of its own and it interns to the same
            // atom, so without this a graphic's script would run as the page's.
            if (txn.tag(at).value_or(atom{}) == script_tag &&
                txn.element_ns(at) == ctbrowser::node_ns::html) {
                // `<script src>` FIRST, then the element's own text - which is
                // what the spec says (a src'd script ignores its content, and
                // an element has one or the other in practice) and what any
                // page carrying a library expects.
                //
                // It goes through the asset registry like every other load, so
                // a test seeds it in memory and a page beside a file finds it
                // on disk. Nothing here fetches over the network: a script that
                // is not in the registry and not beside the page is a page that
                // silently loses a library, so the miss is RECORDED rather than
                // passed over.
                // `type="module"` is the whole difference. Any other value -
                // absent, "text/javascript", "application/json" - is not one;
                // the spec is a fixed string here rather than a MIME match.
                const bool is_module = txn.attribute_value(at, type_attribute) == "module";
                std::string module_text;
                std::string classic_text;
                std::string * into = is_module ? &module_text : &classic_text;
                // THE SPECIFIER A MODULE SCRIPT IS KNOWN BY. `src` gives it a
                // real one; an inline module gets a distinct synthetic one -
                // distinct because it is the registry key, and shared keys made
                // the second inline module on a page silently not run.
                std::string specifier =
                    is_module ? "<inline:" + std::to_string(modules.size()) + ">" : std::string{};

                const std::string_view src = txn.attribute_value(at, atoms_.intern("src"));
                if (!src.empty()) {
                    const std::string url{src};
                    const std::vector<std::byte> bytes = assets_.load(url);
                    if (bytes.empty()) {
                        script_error_ = "<script src=\"" + url + "\"> not found";
                    } else {
                        into->append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                        *into += '\n';
                    }
                    if (is_module) { specifier = url; }
                }
                for (const node_id child : txn.children(at)) { *into += txn.text(child); }
                *into += '\n';
                if (is_module) {
                    modules.emplace_back(std::move(module_text), std::move(specifier));
                } else if (classic_text.find_first_not_of(" \t\r\n\f\v") != std::string::npos) {
                    // A CONTRIBUTION THAT IS ONLY THE NEWLINES THIS WALK ADDED
                    // IS NOT A SCRIPT, and it is dropped HERE rather than
                    // skipped later so that `script_sources()` lists exactly
                    // what gets compiled. `<script src=missing.js></script>`
                    // and `<script></script>` both leave one, and a packager
                    // that built an image for it would be building one nothing
                    // will ever look up.
                    classic_scripts.push_back(std::move(classic_text));
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
    }
    // Kept whether or not any of it is compiled, so a packager can ask what
    // this page would compile without reproducing the rule itself.
    script_sources_ = classic_scripts;
    // AND THE MODULES, so that "this page has scripts no image can cover" is a
    // question something can ask. Nothing here can package them; the point is
    // that the packager and the launcher can both SEE them.
    module_sources_.clear();
    module_sources_.reserve(modules.size());
    for (const auto & [text, specifier] : modules) { module_sources_.push_back(text); }
    // PER LOAD, because that is the question it answers and what its
    // documentation promises: "zero after a load whose every script was
    // cached". Left to accumulate it could not be read against
    // classic_programs_held(), which is per load, and a second load of a fully
    // cached page would still report the first load's misses.
    scripts_compiled_from_source_ = 0;
    // A SCRIPT'S OWN FAILURE OUTRANKS A MISSING <script src>, which the walk
    // above has already written into script_error_. Without this a page with
    // one unresolvable src reported that and nothing else for the rest of its
    // life - the first-failure-wins rule below would find the field already
    // full and never overwrite it, so every later parse error and uncaught
    // throw was silently discarded.
    bool a_script_failed = false;
    for (const std::string & text : classic_scripts) {
        // THE IMAGE FIRST, WHEN IT IS THIS SCRIPT'S. Compiling is about forty
        // percent of a page load; loading the same program from bytes is four
        // times faster on every corpus measured. Two things make it safe, and
        // both are refusals rather than fallbacks: the image's recorded source
        // hash must equal this text's, and it must have been compiled as a
        // classic script - the same text compiled as a module hashes
        // identically and publishes nothing to the page.
        std::unique_ptr<script::program> compiled;
        const std::uint64_t key = script::image_source_hash(text);
        for (const held_image & held : script_images_) {
            if (held.source_hash != key || held.kind != script::script_kind::classic) { continue; }
            auto loaded = script::load_image(held.bytes, key, script::script_kind::classic);
            if (loaded.ok) {
                compiled = std::make_unique<script::program>(std::move(loaded.value));
            }
            break;
        }
        if (compiled == nullptr) {
            ++scripts_compiled_from_source_;
            compiled = std::make_unique<script::program>(script::compiler::compile(text));
        }

        // KEPT BEFORE IT RUNS, because running it is what creates the closures
        // that point into it - a function declared at a script's top level holds
        // a `const function_proto *` into this program, and a timer or a
        // listener dereferences it long after run_scripts returned.
        //
        // The reference survives the push_back, and it is worth saying why
        // rather than leaving it to look wrong: the vector holds unique_ptrs, so
        // a reallocation moves POINTERS. The program itself never moves.
        // THE EMBEDDER'S TURN, BEFORE THE SCRIPT RUNS. A packaged application
        // stamps its compiled bodies onto this program here; nothing else can
        // reach it. See browser::set_script_prepared_hook.
        if (script_prepared_hook_) { script_prepared_hook_(*compiled, text); }
        const script::program & running = *compiled;
        classic_programs_.push_back(std::move(compiled));
        const script::run_result result = script_->run(running);
        // A SCRIPT THAT NAVIGATED TOOK THE PAGE WITH IT. Every later script
        // belongs to a document that is being replaced, so it does not run -
        // and the replacement happens in load_html, after this returns, rather
        // than under our feet.
        if (pending_load_) { return; }
        // THE FIRST FAILURE IS THE ONE REPORTED, and the rest of the page still
        // runs. That is what the specification says: a script that throws or
        // does not parse is that script's problem.
        if (!result.ok && !a_script_failed) {
            script_error_ = result.error;
            a_script_failed = true;
        }
        // AND THE PAGE IS TOLD, every time rather than only the first: a page
        // that listens for `error` is entitled to hear about each script that
        // threw, and `script_error_` above is the embedder's channel, not the
        // page's. Fired here, between scripts, which is where the specification
        // fires it - a later <script> can therefore see that an earlier one
        // died, which is exactly what testharness.js is doing.
        if (!result.ok) {
            // THE FAULT IS CLEARED FIRST, AND THAT IS THE WHOLE TRICK. `run`
            // leaves `failed_` set when a script throws, and every C++ entry
            // into JavaScript refuses to run while it is - so dispatching the
            // event without this called no listener at all, reported the
            // ORIGINAL error a second time as a "callback fault", and left the
            // page believing nothing had gone wrong. The harness then finished
            // normally and a page that threw during load was reported as a
            // PASS, which is the single worst answer this instrument can give.
            //
            // Clearing is not discarding: `result.error` already holds the
            // text and `script_error_` above has recorded it. Handing it to the
            // page is a HANDOFF, and the page cannot be handed anything while
            // the VM is still refusing to run its code.
            (void)script_->take_error();
            (void)bindings_->dispatch_error(result.error);
        }
    }

    // MODULES RUN AFTER THE CLASSIC SCRIPTS, each as its own program in its own
    // scope. Deferred is what the specification says a module script is - it
    // waits for the document rather than running where it sits - and running
    // them last is the shape that will still be right when the loader arrives
    // and they have to wait for their dependencies too.
    //
    // ONE PROGRAM EACH, kept alive: a module's top-level declarations live in
    // its frame, and its functions close over them.
    for (const auto & [module_source, specifier] : modules) {
        load_module(module_source, specifier);
    }
}

// LOAD A MODULE AND EVERYTHING IT NEEDS: instantiate the whole graph, then
// evaluate it. TWO PASSES, and they are two because a cycle cannot be done in
// one.
//
// The single pass this replaced compiled a module, loaded its dependencies and
// ran it, all on the way down. That is right for a tree and wrong for a cycle:
// A imports B imports A, so B ran while A had not reached its first statement,
// and B asked A for an export whose cell did not exist yet. Registering A early
// stopped the descent - but stopping is not binding, and B was told `undefined`.
//
// The specification's answer is to create every binding in the graph BEFORE
// evaluating any of it, so a cycle sees an UNINITIALISED binding rather than a
// missing one. instantiate_module makes the cells; evaluation fills them.
// ADMITTED AT THE DOOR OR NOT AT ALL. Anything that is not an image this build
// would load - wrong magic, another format version, another engine's opcode
// numbering - is refused here and returns false, so a packager learns when it
// hands the bytes over. The alternative is a bag full of images that can never
// match, which presents as "the cache does nothing" months later.
//
// The key comes from the image's OWN header rather than from anything the
// caller says, so a caller cannot file an image under the wrong name.
bool browser::add_script_image(std::vector<std::byte> image) {
    const auto head = script::read_image_header(image);
    if (!head) { return false; }
    for (held_image & held : script_images_) {
        if (held.source_hash != head->source_hash || held.kind != head->kind) { continue; }
        // TWO IMAGES OF ONE SCRIPT, AND ADDING THEM IS ORDER-INDEPENDENT.
        //
        // They can differ in one way and still both be this script's: one keeps
        // `program::source` and one drops it. That is not a detail - it is
        // whether `f.toString()` returns the function's text or
        // "[native code]", and p5's own error system reads its source. Both are
        // valid images and they are NOT interchangeable.
        //
        // Last-writer-wins made the answer depend on the order a packager
        // happened to hand them over: measured, keep-then-drop degraded
        // toString and drop-then-keep did not. THE ONE THAT KEEPS THE SOURCE
        // WINS, whichever arrives first, because the other is an optimisation
        // that removes behaviour and silently taking it when the better image
        // was also offered is the wrong default. `clear_script_images` is how a
        // caller says it means the lean one.
        if (held.option == script::image_option::keep_source &&
            head->option == script::image_option::drop_source) {
            return true;
        }
        held.option = head->option;
        held.bytes = std::move(image);
        return true;
    }
    script_images_.push_back(
        held_image{head->source_hash, head->kind, head->option, std::move(image)});
    return true;
}

void browser::load_module(const std::string & source, const std::string & specifier) {
    if (script_ == nullptr) { return; }
    instantiate_module(source, specifier);
    evaluate_module(specifier);
}

// A RELATIVE SPECIFIER MEANS A DIFFERENT FILE IN EVERY MODULE THAT WRITES IT.
// `./dep.js` inside `sub/a.js` is `sub/dep.js`; the same three words inside the
// page's own module are `dep.js`. Using the specifier as written happens to
// work while everything sits in one directory, which is exactly what makes it
// worth measuring - it passes every earlier rung and fails on the first library
// laid out in folders.
//
// PATHS, NOT URLS, DELIBERATELY. These are asset-registry keys: nothing here
// fetches, so there is no base URL to resolve against yet. When the loader does
// fetch, this becomes real URL resolution - and `docs/plans/ada-url.md` is about
// which standard that should be by. Bare and absolute specifiers pass through
// untouched: a bare one is an import map's business, which is not built.
namespace {
[[nodiscard]] std::string resolve_specifier(std::string_view base, const std::string & spec) {
    if (!spec.starts_with("./") && !spec.starts_with("../")) { return spec; }
    const std::size_t slash = base.rfind('/');
    std::string joined =
        (slash == std::string_view::npos ? std::string{} : std::string{base.substr(0, slash + 1)}) +
        spec;

    std::vector<std::string_view> parts;
    for (std::size_t at = 0; at <= joined.size();) {
        const std::size_t end = joined.find('/', at);
        const std::string_view part{joined.data() + at,
                                    (end == std::string::npos ? joined.size() : end) - at};
        if (part == "..") {
            // ABOVE THE ROOT IT STAYS: `../` out of the top is not something
            // this can answer, and silently dropping it would resolve two
            // different specifiers to the same file.
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else {
                parts.push_back(part);
            }
        } else if (part != "." && !part.empty()) {
            parts.push_back(part);
        }
        if (end == std::string::npos) { break; }
        at = end + 1;
    }

    std::string out;
    for (const std::string_view part : parts) {
        if (!out.empty()) { out += '/'; }
        out += part;
    }
    // KEYED THE WAY THE PAGE WRITES THEM. `./dep.js` and `dep.js` are the same
    // file and must not become two registry entries.
    return out.starts_with("..") ? out : "./" + out;
}
} // namespace

// PASS ONE: compile, register, create the export cells, recurse. Nothing runs.
void browser::instantiate_module(const std::string & source, const std::string & specifier) {
    auto & registry = script_->modules();
    if (registry.find(specifier) != registry.end()) { return; }
    // Registered BEFORE its dependencies, so a cycle finds it and stops rather
    // than descending for ever.
    script::module_record & record = registry[specifier];
    record.specifier = specifier;

    auto compiled = std::make_unique<script::program>(
        script::compiler::compile(source, script::script_kind::module_));
    if (!compiled->ok) {
        if (script_error_.empty()) { script_error_ = specifier + ": " + compiled->error; }
        module_programs_.push_back(std::move(compiled));
        return;
    }
    // EVERY PROTO STAMPED WITH THE SPECIFIER, because a running frame holds a
    // function_proto and nothing else - and `import('./x.js')` called from a
    // callback still has to resolve against the module that wrote it. The
    // compiler cannot do this: a specifier is the loader's name for a file.
    for (script::function_proto & fn : compiled->functions) { fn.module = specifier; }
    // THE CELLS, before a single dependency is even fetched. This is the line
    // the whole two-pass split exists for.
    script_->instantiate_module(*compiled, record);

    // RESOLVED HERE, AND RECORDED, before anything recurses: the bytecode can
    // only carry the specifier as written, so the translation has to live
    // somewhere the VM can read it.
    std::vector<std::string> needed;
    for (const std::string & written : compiled->imports) {
        std::string target = resolve_specifier(specifier, written);
        record.resolved[written] = target;
        needed.push_back(std::move(target));
    }
    module_programs_.push_back(std::move(compiled));

    for (const std::string & target : needed) {
        if (registry.find(target) != registry.end()) { continue; }
        const std::vector<std::byte> bytes = assets_.load(target);
        if (bytes.empty()) {
            if (script_error_.empty()) { script_error_ = "module `" + target + "` not found"; }
            continue;
        }
        instantiate_module(std::string{reinterpret_cast<const char *>(bytes.data()), bytes.size()},
                           target);
    }

    wire_reexports(specifier);
}

// `export { a } from './m.js'` AND `export * from './m.js'`, which is what every
// ES library's index file is made of and what makes one importable at all.
//
// A RE-EXPORT IS AN ALIAS, NOT A COPY: this module's export slot holds the OTHER
// module's cell, so a write there is a read here, through however many index
// files the name travels. Copying the value would break for the same reason
// copying an import does, only further from where anyone would look.
//
// AFTER THE DEPENDENCIES, AND BEFORE ANY EVALUATION. After, because the cell
// being aliased has to exist - and a chain of index files re-exporting each
// other wires deepest-first because the recursion above has already returned.
// Before, because a module that imports the name must find it bound whichever
// order the graph is evaluated in; this is the specification's ResolveExport,
// done where the graph is known rather than in the interpreter.
void browser::wire_reexports(const std::string & specifier) {
    auto & registry = script_->modules();
    const auto self = registry.find(specifier);
    if (self == registry.end() || self->second.compiled == nullptr) { return; }

    // GATHERED FIRST, ASSIGNED AFTER: the registry is a flat_map and writing to
    // one entry while holding a reference into another is how it invalidates.
    std::vector<std::pair<std::string, script::value>> wire;
    for (const auto & edge : self->second.compiled->reexports) {
        const auto mapped = self->second.resolved.find(edge.from);
        const std::string from = mapped == self->second.resolved.end() ? edge.from : mapped->second;
        const auto source = registry.find(from);
        if (source == registry.end()) {
            if (script_error_.empty()) {
                script_error_ = "`export ... from '" + edge.from +
                                "'` names a module that did not "
                                "load";
            }
            continue;
        }
        if (edge.source.empty()) {
            // `export *`, and it does NOT re-export `default` - the
            // specification is explicit about that, and a page relying on it
            // would silently get the wrong module's default.
            for (const auto & [name, cell] : source->second.exports) {
                if (name != "default") { wire.emplace_back(name, cell); }
            }
            continue;
        }
        const auto cell = source->second.exports.find(edge.source);
        if (cell == source->second.exports.end()) {
            if (script_error_.empty()) {
                script_error_ = "`" + from + "` has no export named `" + edge.source + "`";
            }
            continue;
        }
        wire.emplace_back(edge.exported, cell->second);
    }

    const auto into = registry.find(specifier);
    if (into == registry.end()) { return; }
    for (auto & [name, cell] : wire) {
        // A NAME THIS MODULE DECLARES ITSELF WINS. `export *` is the weakest
        // claim on a name - an explicit export shadows it, and so does an
        // earlier explicit re-export.
        if (into->second.exports.find(name) == into->second.exports.end()) {
            into->second.exports[name] = cell;
        }
    }
}

// PASS TWO: depth-first POST-ORDER, each module once. A dependency has finished
// running before the module that imports it reaches its own first statement -
// except around a cycle, where by definition one of them cannot, and where the
// cells from pass one are what keeps that from being a missing export.
void browser::evaluate_module(const std::string & specifier) {
    auto & registry = script_->modules();
    const auto found = registry.find(specifier);
    if (found == registry.end() || found->second.evaluated || found->second.compiled == nullptr) {
        return;
    }
    // MARKED BEFORE THE DEPENDENCIES, not after: around a cycle the recursion
    // arrives back here, and a flag set afterwards would let it descend for
    // ever. This is the specification's `evaluating` status under another name.
    found->second.evaluated = true;
    const script::program * const compiled = found->second.compiled;
    std::vector<std::string> needed;
    for (const std::string & written : compiled->imports) {
        const auto mapped = found->second.resolved.find(written);
        needed.push_back(mapped == found->second.resolved.end() ? written : mapped->second);
    }
    for (const std::string & target : needed) { evaluate_module(target); }

    // RE-FOUND, because the registry is a flat_map and the recursion above can
    // insert into it - a reference taken before it would dangle.
    const auto self = registry.find(specifier);
    if (self == registry.end()) { return; }
    const script::run_result result = script_->run_module(*compiled, self->second);
    if (!result.ok && script_error_.empty()) { script_error_ = result.error; }
}

void browser::install_embedder_natives() {
    for (const auto & [name, fn] : embedder_natives_) { script_->define_native(name, fn); }

    // WHAT `import(specifier)` CALLS. The VM hands over the specifier and the
    // module that wrote it; resolving, finding the bytes and walking the graph
    // are all this side's job, which is why the hook exists at all.
    //
    // IT SETTLES IMMEDIATELY, and that is a limitation rather than a design:
    // the asset registry is synchronous, so there is nothing to wait for. A
    // real fetch makes this a pending promise settled later, and the promise is
    // already the right shape for that - which is why the return type is a
    // promise now rather than the namespace itself.
    script_->set_module_loader([this](script::context & cx, const std::string & specifier,
                                      const std::string & referrer) {
        const std::string target = resolve_specifier(referrer, specifier);
        auto & registry = cx.modules();
        if (registry.find(target) == registry.end()) {
            const std::vector<std::byte> bytes = assets_.load(target);
            if (bytes.empty()) {
                return cx.make_promise(cx.make_error("Error", "module `" + target + "` not found"),
                                       true);
            }
            load_module(std::string{reinterpret_cast<const char *>(bytes.data()), bytes.size()},
                        target);
        } else {
            // ALREADY IN THE REGISTRY is not already EVALUATED - a module can
            // be instantiated and waiting, and importing it dynamically has to
            // run it rather than hand back empty bindings.
            evaluate_module(target);
        }
        const auto found = registry.find(target);
        if (found == registry.end()) {
            return cx.make_promise(cx.make_error("Error", "module `" + target + "` did not load"),
                                   true);
        }
        return cx.make_promise(cx.module_namespace(found->second), false);
    });
}

} // namespace ctbrowser::shell
