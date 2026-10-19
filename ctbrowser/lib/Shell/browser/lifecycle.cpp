//===- ctbrowser/lib/Shell/browser/lifecycle.cpp - Page lifecycle ---------===//
//
// A page's lifecycle: loading a document (and the navigation a script queues
// from inside one), the reset between pages, and what an embedder installs
// on a browser: natives, fonts, hooks, a script to run.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

void browser::load_html(std::string_view html) {
    load_document(html, source_kind::html);
}

void browser::load_document(std::string_view html, source_kind kind) {
    // A navigation from inside a script is queued, not performed.
    // `element.click()` reaches `browser::activate`, which calls the
    // embedder's navigate hook, and an embedder's hook (ctbrowse's, for one)
    // calls this, so this function can be re-entered from inside a script that
    // `run_scripts` is running.
    //
    // Loading immediately would reset `script_` and free the running programs
    // while the interpreter is still executing one of them (use-after-free),
    // and would let the old page's remaining scripts run against the new
    // document.
    //
    // Deferring is also what a browser does: navigation is not synchronous.
    // The script that asked for it finishes, `run_scripts` stops at the next
    // boundary because a load is pending, and the load happens below, once
    // nothing is running on the context it is about to destroy.
    if (loading_) {
        pending_load_ = std::string{html};
        pending_kind_ = kind;
        return;
    }
    loading_ = true;
    load_one_page(html, kind);
    loading_ = false;
    // A queued navigation, and any it queues in turn. There is no cap: a page
    // that navigates on load forever hangs in a real browser too.
    while (pending_load_) {
        std::string next = std::move(*pending_load_);
        const source_kind next_kind = pending_kind_;
        pending_load_.reset();
        loading_ = true;
        load_one_page(next, next_kind);
        loading_ = false;
    }
}

void browser::load_one_page(std::string_view html, source_kind kind) {
    source_html_ = html; // What location.reload() re-runs.
    source_kind_ = kind;
    // Both the document and the cascade are rebuilt. Keeping the old style
    // engine would accumulate every page's <style> rules across navigations,
    // which shows up as the previous page bleeding into the next one.
    reset_document();
    // XML and HTML are two front ends, not one with a flag. See dom/xml.hpp:
    // nothing is implied, every tag may self-close, case is preserved, and a
    // `<script>` written as `<![CDATA[ ... ]]>` is code rather than code with
    // a marked section on the front. A malformed XML document does not
    // recover (there is nothing to recover to), so the error is kept and the
    // partial tree is loaded, which lets an embedder show it.
    xml_error_.clear();
    parse_result parsed;
    if (kind == source_kind::xml) {
        xml_parse_result read = parse_xml(*doc_, html);
        parsed = std::move(read.tree);
        xml_error_ = std::move(read.error);
    } else if (const std::string declared = prescan_encoding(html); !declared.empty()) {
        // What the page declares it is encoded as (the BOM or the <meta>
        // prescan) is what `document.characterSet` answers; UTF-8 otherwise.
        // The HTML itself is parsed later by run_scripts, because the parse
        // and the scripts are one process: HTML 13.2.6.4.8 stops the parser at
        // every `</script>` and runs it against the tree built so far, and
        // `document.write` from it goes into the parser's input stream. See
        // bindings/document/write.cpp.
        doc_->set_encoding(declared);
    }
    scroll_y_ = 0;
    scroll_x_ = 0;
    author_sheet_loaded_ = false;
    style_error_.clear();
    resource_loads_.clear();
    announced_loads_.clear();
    // Before the parse, because it clears the SVG store the parse fills. The
    // <img> walk it does is redone before every layout anyway
    // (refresh_images), which is where the elements the parse made are seen.
    load_images();
    for (const auto & [id, source] : parsed.svg_sources) { svg_.set_source(id, source); }
    mark(dirty::everything);
    // The parse and the scripts. An XML document's tree is already built; an
    // HTML page's is built here, script by script.
    run_scripts();
    title_ = extract_title();
    // Styles, images and fonts are resolved before layout, because an <img>
    // with no width attribute takes its size from the decoded bitmap and
    // layout has no way to ask; the page's @font-face files likewise. Load the
    // sheets the last script did not see, or all of them on a page with no
    // script.
    if (!author_sheet_loaded_) {
        load_author_styles();
    } else {
        refresh_author_styles();
    }
    load_page_fonts();
    // The sheets and scripts above are owed their `load`. Handed over here
    // because run_scripts has only just built the bindings that queue them.
    announce_resource_loads();
    // The page has loaded. That is announced on the next tick, not here; see
    // browser::load_event_pending_ for why the delay is deliberate.
    load_event_pending_ = true;
}

void browser::reset_document() {
    doc_ = std::make_unique<document>(atoms_);
    styles_ = std::make_unique<ctbrowser::style::engine>(atoms_);
    install_text_measure();
    // Before the first sheet, so its conditions are evaluated against the real
    // viewport rather than against the 1024x768 default and then corrected.
    (void)media_environment_changed();
    styles_->add_sheet(ctbrowser::style::ua_css, ctbrowser::style::ua_origin);
    // The style engine object is new, and bindings that outlive this call
    // would otherwise match selectors through the freed one.
    if (bindings_) { bindings_->observe_style_engine(*styles_); }
    resolved_.clear();
}

void browser::reload() {
    const std::string source = source_html_;
    // Copied: load_document overwrites source_html_.
    load_document(source, source_kind_);
}

void browser::define_native(std::string name, script::native_fn fn) {
    for (auto & [existing, handler] : embedder_natives_) {
        if (existing == name) {
            handler = std::move(fn);
            return;
        }
    }
    embedder_natives_.emplace_back(std::move(name), std::move(fn));
    if (script_) { install_embedder_natives(); }
}

/// The directory holding the vendored font faces: $CTBROWSER_FONT_PATH, or
/// "fonts". It is a function rather than a few lines inside use_real_fonts
/// because a packager must ask the same question: the faces are loaded
/// through the asset registry under names that begin with this directory, so
/// an application that bakes them in must use the names the run will ask
/// for, and a second copy of this rule could drift.
std::string browser::default_font_directory() {
    const char * from_env = std::getenv("CTBROWSER_FONT_PATH");
    return from_env != nullptr ? std::string{from_env} : std::string{"fonts"};
}

bool browser::use_real_fonts(std::string_view directory) {
    // An empty directory means "use default_font_directory()". It is resolved
    // here, not as a default argument (which cannot read the environment), so
    // no caller has to repeat the rule.
    std::string resolved;
    if (directory.empty()) {
        resolved = default_font_directory();
        directory = resolved;
    }
    std::unique_ptr<ctbrowser::raster::ttf_backend> backend = ctbrowser::raster::make_ttf_backend();
    if (!backend) { return false; }
    // Register the faces embedded in the binary, if any, under the same names
    // the loop below loads, so the loop does not care whether a face came from
    // the binary or the disk. Without embedded faces it reads the directory.
    (void)register_embedded_fonts(assets_, directory);
    // The embedder named this directory, not the page, so a confined registry
    // may read from it wherever it is.
    assets_.add_root(std::filesystem::path{directory});
    // A CSS family name and the file stem of its four (bold, italic) files.
    struct vendored {
        std::string_view family;
        std::string_view stem;
    };
    for (const vendored & face :
         {vendored{"serif", "Tinos"}, vendored{"Tinos", "Tinos"},
          vendored{"sans-serif", "FiraSans"}, vendored{"Fira Sans", "FiraSans"},
          vendored{"monospace", "Cousine"}, vendored{"Cousine", "Cousine"}}) {
        for (const auto & [bold, italic, suffix] :
             {std::tuple{false, false, "Regular"}, std::tuple{true, false, "Bold"},
              std::tuple{false, true, "Italic"}, std::tuple{true, true, "BoldItalic"}}) {
            const std::string path =
                std::string{directory} + "/" + std::string{face.stem} + "-" + suffix + ".ttf";
            const std::vector<std::byte> bytes = assets_.load(path);
            if (!bytes.empty()) {
                (void)backend->add_face(std::string{face.family}, bold, italic, bytes);
            }
        }
    }
    if (backend->face_count() == 0) { return false; }
    backend->set_default_family("serif"); // What the UA sheet gives <body>.
    ttf_ = std::move(backend);
    load_page_fonts();
    fonts_ = ttf_.get();
    renderer_.fonts = fonts_;
    // The canvas measures and draws its own text, so it needs the same
    // backend; otherwise a page's canvas keeps the bitmap font while the
    // document around it switches to real faces.
    canvases_.set_fonts(fonts_);
    // Everything measured so far was measured with the other font.
    mark(dirty::everything);
    return true;
}

void browser::allow_network(bool allowed) {
    network_allowed_ = allowed;
    if (bindings_) { bindings_->allow_network(allowed); }
}

bool browser::run_script(std::string_view source) {
    return run_script_impl(source, nullptr);
}

bool browser::run_script_impl(std::string_view source, script::value * thrown) {
    if (!script_) { return false; }
    auto compiled =
        std::make_unique<script::program>(script::compiler::compile(std::string{source}));
    if (!compiled->ok) {
        script_error_ = compiled->error;
        if (thrown != nullptr) {
            *thrown = compiled->error.starts_with("parse error:")
                          ? script_->make_error("SyntaxError", compiled->error)
                          : script::value::undefined();
        }
        return false;
    }
    // Kept, not scoped. A closure holds a `const function_proto *` into its
    // program, and anything that calls back later (a timer, a listener,
    // requestAnimationFrame) dereferences it long after this returns.
    const script::program & kept = *compiled;
    extra_programs_.push_back(std::move(compiled));
    const script::run_result result = script_->run(kept);
    if (!result.ok && thrown != nullptr) { *thrown = script_->last_thrown(); }
    // Reset on every run so that one failing script does not make later
    // successful ones report its error; callers use this to tell whether the
    // script ran.
    script_error_ = result.ok ? std::string{} : result.error;
    // A successful run does not erase a fault it did not cause. Callbacks
    // report through the same field, and running any script afterwards (a
    // test's probe, a devtools eval) would otherwise clear the one message
    // saying why the page stopped responding.
    if (script_error_.empty() && bindings_ && !bindings_->callback_error().empty()) {
        script_error_ = bindings_->callback_error();
    }
    return result.ok;
}

std::size_t browser::live_script_objects() const {
    return script_ ? script_->live_objects() : 0;
}

void browser::set_alert_hook(std::function<void(const std::string &)> hook) {
    alert_hook_ = std::move(hook);
}

void browser::set_navigate_hook(
    std::function<void(const std::string &, const std::string &, bool)> hook) {
    navigate_hook_ = std::move(hook);
}

} // namespace ctbrowser::shell
