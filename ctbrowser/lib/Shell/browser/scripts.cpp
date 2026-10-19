//===- ctbrowser/lib/Shell/browser/scripts.cpp - Page scripts -------------===//
//
// The page's scripts: every classic <script> as its own program, ES modules
// in two passes over the import graph, precompiled script images, and the
// natives and module loader the browser installs on the script context.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

void browser::report_script_error(std::string_view error, std::string_view specifier) {
    // JavaScript listeners cannot enter while the VM's failure flag is set.
    // The caller keeps the diagnostic alive across error-listener callbacks.
    (void)script_->take_error();
    (void)bindings_->dispatch_error(error, specifier);
}

bool browser::run_page_script(std::string_view source, std::string_view specifier) {
    if (!script_) { return false; }
    const bool ok = run_script(source);
    if (!ok) {
        const std::string error = script_error_;
        report_script_error(error, specifier);
    }
    return ok;
}

void browser::run_scripts() {
    // Order matters on teardown too: the old context must go before the old
    // program it was executing.
    script_.reset();
    classic_programs_.clear();
    // Drop the previous page's module programs too; module functions close
    // over their program, so these are kept alive only by this vector and would
    // otherwise pile up across reloads.
    module_programs_.clear();
    script_ = std::make_unique<script::context>();
    // The standard library goes in first, so a page's own globals can
    // shadow it rather than the other way round.
    script::install_builtins(*script_);
    // The page's wall clock: a fixed base plus how long this page has been
    // running, so `Date.now()` advances, reads as a plausible instant, and is
    // still identical on every run (`tick()` moves the page clock by exactly
    // what it was given). An embedder that wants real time calls set_clock,
    // as the SDL app does. See context::set_clock.
    script_->set_clock([this] {
        return clock_ ? clock_() : script::context::fixed_epoch_base + bindings_->now_ms();
    });
    canvases_.clear();
    forms_.clear();
    focused_ = node_id{};
    // The in-flight drag goes with the document. These are handles into a slab
    // that has just been rebuilt, and forms_.state_of would seed fresh state
    // for a stale one, so a page that reloads mid-drag would otherwise keep
    // auto-scrolling a field that no longer exists.
    field_selecting_ = node_id{};
    pressed_ = node_id{};
    selecting_ = false;
    // The frames' layouts point into the bindings being replaced; a stale
    // pointer there would be followed on the next layout_frames.
    frame_layouts_.clear();
    bindings_ = std::make_unique<dom_bindings>(
        *doc_, atoms_, canvases_, forms_,
        [this] {
            // A script mutation marks `dirty::styles`, not `paint`: any DOM
            // write can change which rules match, and flush_for_read
            // (getComputedStyle, layout reads) re-runs only what the dirty
            // level says, so a lower level would answer from the styles before
            // the write.
            mark(dirty::styles);
            // HTML's focus fixup rule: a focused element that left the
            // document is focused no more, and the next `focus()` on it is a
            // real one (WPT moveBefore/fire-focusin-focusout.html re-focuses a
            // button its cleanup re-appended). No events fire, as the rule
            // says.
            if (focused_ && (!bindings_->is_connected(focused_) || focus_was_moved())) {
                (void)set_state(focused_, state_focus, false);
                focused_ = node_id{};
                bindings_->observe_focus(focused_);
            }
        },
        [this](node_id id) { (void)focus(id); });
    // The cascade's engine, so `querySelector` runs the matcher a stylesheet
    // runs. reset_document() replaces it on every load and this object is
    // rebuilt after that, so handing it over here is enough; the second call
    // in reset_document covers a reload that keeps these bindings.
    bindings_->observe_style_engine(*styles_);
    bindings_->observe_viewport(layout_viewport_width(), options_.height);
    bindings_->observe_resources(assets_, images_);
    bindings_->observe_requests(&options_.on_request);
    bindings_->allow_network(network_allowed_);
    // `element.click()` performs the default action, which is the browser's half.
    bindings_->set_activate_hook([this](node_id id) { activate(id); });
    bindings_->set_alert_hook([this](const std::string & message) {
        alerts_.push_back(message);
        if (alert_hook_) { alert_hook_(message); }
    });
    // CSSOM edits reach the cascade through this hook. `insertRule`,
    // `selectorText =`, `replaceSync` and `adoptedStyleSheets` all funnel
    // through `style_sheets_changed`, which is called from nowhere else
    // (reading `document.styleSheets` does not fire it), so a page that never
    // uses the object model keeps `refresh_author_styles`' DOM-text path. The
    // text handed back is the sheets re-serialised from their compiled form;
    // `representable` in bindings/stylesheets keeps the author's bytes for any
    // selector this front end cannot reproduce, which makes that safe.
    //
    // `author_css_` is left alone: it is the key `refresh_author_styles`
    // compares the DOM's text against, and the DOM's text has not changed, so
    // the next restyle does not re-collect the `<style>` elements and undo
    // this. The cost is one narrow case: a page that mutates the CSSOM and
    // then rewrites a different `<style>`'s text falls back to the DOM's
    // until its next CSSOM write.
    bindings_->set_author_styles_hook([this](std::string css) {
        if (!author_sheet_loaded_) { return; }
        styles_->clear_origin(ctbrowser::style::author_origin);
        if (!css.empty()) { styles_->add_sheet(css, ctbrowser::style::author_origin); }
        // The restyle is scheduled by the `mutated()` that follows inside
        // `style_sheets_changed`, the same funnel every other DOM write uses.
    });
    bindings_->observe_location(location_href_, location_hash_);
    bindings_->install(*script_);
    // getComputedStyle must see the page as the script just left it, but the
    // computed style comes from `resolved_`, `boxes_` and `fragments_`, which
    // run_layout hands over: they do not exist yet while load-time scripts
    // run, and are stale after any style write. So the global is wrapped here
    // to call flush_for_read() first, which re-runs only the pipeline stages
    // that are dirty (a second call with no write in between costs nothing)
    // and leaves the level at `paint`, since the display list must still be
    // re-recorded before anything is drawn.
    //
    // It is wrapped here, not flushed inside the binding, because the bindings
    // do not know layout exists ("a native that changes the document calls
    // on_mutation, and the browser decides what that invalidates", at the top
    // of shell/bindings.hpp). install_embedder_natives runs after this, so an
    // embedder that defines its own `getComputedStyle` still wins.
    if (const script::value inner = script_->global("getComputedStyle"); inner.is_callable()) {
        auto * flushing = script_->allocate<script::native_object>(
            "getComputedStyle", [this, inner](script::context & c, std::span<script::value> args) {
                flush_for_read();
                return c.call(inner, args);
            });
        // `inner` lives in a C++ capture, which the precise collector cannot
        // see, and the global that held it has just been overwritten, so this
        // is the only reference left to it. See script::native_object::retained,
        // which exists for exactly this and says why a property would be worse.
        flushing->retained.push_back(inner);
        script_->define_global("getComputedStyle", script::value::object(flushing));
    }
    // The same flush for every box geometry a script reads; see set_layout_hook.
    bindings_->set_layout_hook([this] { flush_for_read(); });
    bindings_->set_viewport_scroll_hooks([this] { return scroll_position(); },
                                         [this](point p) { scroll_to(p.x, p.y); });
    install_embedder_natives();
    // What a frame's realm starts as: every global the page has before its
    // first script (the builtins, the platform's interfaces, the embedder's
    // natives) and none of the page's own `var`s (bindings/frames.cpp).
    script_->set_realm_template(script_->globals());
    script_error_.clear();

    // The document is parsed below, and each script runs as the parser reaches
    // it (HTML 13.2.6.4.8 and 8.4; see bindings/document/write.cpp). Each
    // classic <script> is its own program (one Script Record each), so a parse
    // error or uncaught throw in one does not stop the next, and an earlier
    // script cannot call a function declared in a later one (a ReferenceError,
    // as in Chrome). Scripts are compiled here, in the browser, so the image
    // cache, the packager's prepared hook and `script_sources()` see them.
    //
    // Modules go through load_module, each as its own program in its own
    // scope, deferred to the end of the parse as the specification says.
    script_sources_.clear();
    module_sources_.clear();
    // Reset per load, as its documentation promises ("zero after a load whose
    // every script was cached"), so it can be read against
    // classic_programs_held(), which is also per load.
    scripts_compiled_from_source_ = 0;
    bindings_->set_current_script(node_id{});
    // The parser's scripts see "loading"; the end of the parse moves it on.
    bindings_->set_ready_state("loading");
    // A script's own failure outranks a missing <script src>: the first
    // failure is the one `script_error_` reports, and a missing src is
    // recorded only while no script has failed yet.
    const auto runner = [this, a_script_failed = false, running = std::vector<node_id>{}](
                            const dom_bindings::parser_script & script) mutable {
        if (script.missing) {
            if (!a_script_failed) {
                script_error_ = "<script src=\"" + script.specifier + "\"> not found";
            }
            return true;
        }
        if (script.module) {
            module_sources_.push_back(script.source);
            load_module(script.source, script.specifier);
            return !pending_load_;
        }
        const std::string & text = script.source;
        if (text.find_first_not_of(" \t\r\n\f\v") == std::string::npos) {
            // Whitespace is not a script. It is dropped here so that
            // `script_sources()` lists exactly what gets compiled; a packager
            // would otherwise build an image nothing ever looks up.
            return true;
        }
        script_sources_.push_back(text);
        // Apply the sheets before this script. A <style> or <link> the parser
        // has already inserted is in effect when this script runs (HTML
        // "update a style block" at the end tag), so its getComputedStyle and
        // offsetWidth read the styled page; the sheets that follow it are not,
        // as in a browser. The first script latches `author_sheet_loaded_`;
        // each later one re-collects what the parse added since. Their `load`
        // events are handed over now, ahead of this script's own, so the
        // events keep document order.
        if (!author_sheet_loaded_) {
            load_author_styles();
        } else {
            refresh_author_styles();
        }
        announce_resource_loads();
        // Use a precompiled image of this script when one was added (see
        // add_script_image): loading a program from bytes is several times
        // faster than compiling it. An image is used only when its recorded
        // source hash equals this text's and it was compiled as a classic
        // script; the same text compiled as a module hashes identically but
        // publishes nothing to the page.
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
        // The embedder's turn, before the script runs: a packaged application
        // stamps its compiled bodies onto this program here; nothing else can
        // reach it. See browser::set_script_prepared_hook.
        //
        // The program is kept (in classic_programs_) before it runs, because
        // running it creates closures that point into it: a function declared
        // at a script's top level holds a `const function_proto *` into this
        // program, and a timer or a listener dereferences it long after
        // run_scripts returns. The vector holds unique_ptrs, so a reallocation
        // moves pointers; the program itself never moves.
        if (script_prepared_hook_) { script_prepared_hook_(*compiled, text); }
        const script::program & running_program = *compiled;
        classic_programs_.push_back(std::move(compiled));
        // A <script> written by document.write runs nested inside the script
        // that wrote it, so it goes through call_fenced (as
        // run_inserted_scripts in bindings/document/entry.cpp does) and its
        // throw is reported here instead of unwinding into the writer; a
        // top-level script goes through run(), which clears the frame and
        // handler stacks. Nesting is tracked with `running`, not
        // current_stack().empty(): a script that overflowed the call stack
        // leaves frames behind. document.currentScript follows `running`: it
        // is this element while it runs and while its error is reported, and
        // the outer script's again afterwards.
        const bool nested = !running.empty() || script_->in_native();
        running.push_back(script.element);
        bindings_->set_current_script(script.element);
        script::run_result result;
        if (!nested) {
            result = script_->run(running_program);
        } else {
            for (const std::string & name : running_program.hoisted_vars) {
                script_->declare_global_var(name, false);
            }
            auto * entry = script_->allocate<script::closure_object>(&running_program.functions[0]);
            entry->owner = &running_program;
            bool threw = false;
            value thrown = value::undefined();
            (void)script_->call_fenced(value::object(entry), {}, script_->global_this(), threw,
                                       thrown);
            result.ok = !threw && !script_->failed();
            if (!result.ok) {
                const script::context::rooted keep_thrown{*script_, thrown};
                result.error =
                    script_->failed()
                        ? script_->take_error()
                        : "uncaught " +
                              (thrown.is_object()
                                   ? script_->to_string(script_->lookup_property(thrown, "message"))
                                   : script_->to_string(thrown));
            }
        }
        // A script that navigated took the page with it. Every later script
        // belongs to a document that is being replaced, so the parser stops,
        // and the replacement happens in load_document after this returns.
        if (pending_load_) {
            running.clear();
            bindings_->set_current_script(node_id{});
            return false;
        }
        // The first failure is the one reported, and the rest of the page still
        // runs, as the specification says: a script that throws or does not
        // parse affects only itself.
        if (!result.ok && !a_script_failed) {
            script_error_ = result.error;
            a_script_failed = true;
        }
        // The page is told about every failure, not only the first: a page
        // that listens for `error` hears about each script that threw, while
        // `script_error_` above is the embedder's channel. It fires here,
        // between scripts, where the specification fires it, so a later
        // <script> can see that an earlier one died (testharness.js relies on
        // this).
        if (!result.ok) {
            // Still the current script while its error is reported: a
            // window.onerror reading document.currentScript sees the script
            // that failed, even one that failed to parse (WPT
            // Document.currentScript.html asserts this by id).
            report_script_error(result.error, script.specifier);
        }
        running.pop_back();
        bindings_->set_current_script(running.empty() ? node_id{} : running.back());
        return true;
    };
    bindings_->set_script_runner(runner);

    if (source_kind_ != source_kind::xml) {
        parse_result parsed = bindings_->parse_document(source_html_);
        // An inline <svg>'s source came from the parse rather than from a
        // file, but from here on the two are the same thing: a graphic to
        // rasterise at whatever size its box turns out to be.
        for (const auto & [id, source] : parsed.svg_sources) { svg_.set_source(id, source); }
        return;
    }

    // An XHTML document's scripts. XML has no parser-driven insertion point
    // and no document.write (it throws InvalidStateError there), so the whole
    // tree was built first (lifecycle.cpp) and every <script> in it runs
    // afterwards, in document order: classic scripts as they come, modules
    // after them, through the same runner.
    std::vector<dom_bindings::parser_script> classic;
    std::vector<dom_bindings::parser_script> modules;
    {
        const auto txn = doc_->read();
        const atom script_tag = atoms_.intern_lower("script");
        const atom type_attribute = atoms_.intern("type");
        const auto walk = [&](auto && self, node_id at) -> void {
            const bool is_svg = txn.element_ns(at) == ctbrowser::node_ns::svg;
            if (txn.tag(at).value_or(atom{}) == script_tag &&
                (txn.element_ns(at) == ctbrowser::node_ns::html || is_svg)) {
                dom_bindings::parser_script script{at, {}, {}, false, false};
                script.module = txn.attribute_value(at, type_attribute) == "module";
                std::string_view src =
                    txn.attribute_value(at, atoms_.intern(is_svg ? "href" : "src"));
                if (is_svg && src.empty()) {
                    src = txn.attribute_value(at, atoms_.intern("xlink:href"));
                }
                if (!src.empty()) {
                    const std::vector<std::byte> bytes = assets_.load(std::string{src});
                    if (bytes.empty()) {
                        script.missing = true;
                    } else {
                        script.source.assign(reinterpret_cast<const char *>(bytes.data()),
                                             bytes.size());
                        script.source += '\n';
                    }
                    script.specifier = std::string{src};
                    note_resource_load(at, !script.missing);
                }
                for (const node_id child : txn.children(at)) { script.source += txn.text(child); }
                script.source += '\n';
                if (script.module && script.specifier.empty()) {
                    script.specifier = "<inline:" + std::to_string(modules.size()) + ">";
                }
                if (script.module) {
                    modules.push_back(std::move(script));
                } else if (src.empty() && script.source.size() <= 1) {
                    bindings_->note_unstarted_script(at);
                } else {
                    classic.push_back(std::move(script));
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
    }
    std::function<bool(const dom_bindings::parser_script &)> run = runner;
    for (const std::vector<dom_bindings::parser_script> * batch : {&classic, &modules}) {
        for (const dom_bindings::parser_script & script : *batch) {
            if (!run(script)) { return; }
        }
    }
    bindings_->set_ready_state("interactive");
}

/// Adds a precompiled script image for run_scripts to use instead of
/// compiling. Anything that is not an image this build would load (wrong
/// magic, another format version, another engine's opcode numbering) is
/// refused here with false, so a packager learns of it when it hands the bytes
/// over rather than finding later that the cache never matches.
///
/// The key comes from the image's own header rather than from anything the
/// caller says, so a caller cannot file an image under the wrong name.
bool browser::add_script_image(std::vector<std::byte> image) {
    const auto head = script::read_image_header(image);
    if (!head) { return false; }
    for (held_image & held : script_images_) {
        if (held.source_hash != head->source_hash || held.kind != head->kind) { continue; }
        // Two images of the same script can differ only in whether they keep
        // `program::source`, which decides whether `f.toString()` returns the
        // source text or "[native code]" (p5 reads its own source). The one
        // that keeps the source wins whichever arrives first.
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

/// Loads a module and its whole import graph in two passes: first every
/// module is compiled and its export bindings created (instantiate_module),
/// then the graph is evaluated (evaluate_module). Creating every binding
/// before running any code means that in an import cycle (A imports B imports
/// A) B sees A's export as an uninitialised binding rather than a missing one.
void browser::load_module(const std::string & source, const std::string & specifier) {
    if (script_ == nullptr) { return; }
    instantiate_module(source, specifier);
    evaluate_module(specifier);
}

namespace {
/// Resolves a module specifier against the module that wrote it: `./dep.js`
/// inside `sub/a.js` is `sub/dep.js`, but inside the page's own module it is
/// `dep.js`. These are asset-registry paths, not URLs: nothing here fetches,
/// so there is no base URL yet (see docs/history/ada-url.md for the URL
/// standard to use once it does). Bare and absolute specifiers pass through
/// unchanged because import maps are not implemented.
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
            // A `..` above the root is kept: this cannot resolve it, and
            // silently dropping it would resolve two different specifiers to
            // the same file.
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
    // Keyed the way the page writes them, with a leading `./`: `./dep.js` and
    // `dep.js` are the same file and must not become two registry entries.
    return out.starts_with("..") ? out : "./" + out;
}
} // namespace

/// First module pass: compile, register, create the export cells, and recurse
/// into the imports. Nothing runs.
void browser::instantiate_module(const std::string & source, const std::string & specifier) {
    auto & registry = script_->modules();
    if (registry.find(specifier) != registry.end()) { return; }
    // Registered before its dependencies, so a cycle finds it and stops rather
    // than descending forever.
    script::module_record & record = registry[specifier];
    record.specifier = specifier;

    auto compiled = std::make_unique<script::program>(
        script::compiler::compile(source, script::script_kind::module_));
    if (!compiled->ok) {
        if (script_error_.empty()) { script_error_ = specifier + ": " + compiled->error; }
        module_programs_.push_back(std::move(compiled));
        return;
    }
    // Stamp every function_proto with the specifier: a running frame holds a
    // function_proto and nothing else, and `import('./x.js')` called from a
    // callback still has to resolve against the module that wrote it. The
    // compiler cannot do this; a specifier is the loader's name for a file.
    for (script::function_proto & fn : compiled->functions) { fn.module = specifier; }
    // Create the export cells before any dependency is fetched; this is what
    // the two-pass split exists for.
    script_->instantiate_module(*compiled, record);

    // Resolve and record each import before recursing: the bytecode can only
    // carry the specifier as written, so the translation has to live
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

/// Wires `export { a } from './m.js'` and `export * from './m.js'`, which is
/// what every ES library's index file is made of.
///
/// A re-export is an alias, not a copy: this module's export slot holds the
/// other module's cell, so a write there is visible here, through however many
/// index files the name travels.
///
/// Runs after the dependencies are instantiated, because the aliased cell has
/// to exist (a chain of index files wires deepest-first, since the recursion
/// has already returned), and before any evaluation, because a module that
/// imports the name must find it bound whatever order the graph is evaluated
/// in. This is the specification's ResolveExport, done where the graph is
/// known rather than in the interpreter.
void browser::wire_reexports(const std::string & specifier) {
    auto & registry = script_->modules();
    const auto self = registry.find(specifier);
    if (self == registry.end() || self->second.compiled == nullptr) { return; }

    // Gathered first, assigned after: a module can re-export from itself, and
    // adding to its exports (a flat_map) while walking them invalidates the walk.
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
            // `export *` does not re-export `default`; the specification is
            // explicit about that.
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
        // A name this module declares itself wins. `export *` is the weakest
        // claim on a name: an explicit export shadows it, and so does an
        // earlier explicit re-export.
        if (into->second.exports.find(name) == into->second.exports.end()) {
            into->second.exports[name] = cell;
        }
    }
}

/// Second module pass: evaluates the graph depth-first in post-order, each
/// module once. A dependency has finished running before the module that
/// imports it reaches its first statement, except around a cycle, where one of
/// them cannot; there the cells from the first pass keep that from being a
/// missing export.
void browser::evaluate_module(const std::string & specifier) {
    auto & registry = script_->modules();
    const auto found = registry.find(specifier);
    if (found == registry.end() || found->second.evaluated || found->second.compiled == nullptr) {
        return;
    }
    // Marked before the dependencies, not after: around a cycle the recursion
    // arrives back here, and a flag set afterwards would let it descend
    // forever. This is the specification's `evaluating` status under another
    // name.
    found->second.evaluated = true;
    const script::program * const compiled = found->second.compiled;
    std::vector<std::string> needed;
    for (const std::string & written : compiled->imports) {
        const auto mapped = found->second.resolved.find(written);
        needed.push_back(mapped == found->second.resolved.end() ? written : mapped->second);
    }
    for (const std::string & target : needed) { evaluate_module(target); }

    // Looked up again, because the registry is an unordered_map and the
    // recursion above can insert into it; a rehash invalidates an iterator
    // taken before it.
    const auto self = registry.find(specifier);
    if (self == registry.end()) { return; }
    const script::run_result result = script_->run_module(*compiled, self->second);
    if (!result.ok && script_error_.empty()) { script_error_ = result.error; }
}

void browser::install_embedder_natives() {
    for (const auto & [name, fn] : embedder_natives_) { script_->define_native(name, fn); }

    // What `import(specifier)` calls. The VM hands over the specifier and the
    // module that wrote it; resolving, finding the bytes and walking the graph
    // are the browser's job.
    //
    // The promise settles immediately because the asset registry is
    // synchronous. A real fetch would make it a pending promise settled later,
    // which is why this returns a promise rather than the namespace itself.
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
            // Being in the registry does not mean evaluated: a module can be
            // instantiated and waiting, and importing it dynamically has to
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
