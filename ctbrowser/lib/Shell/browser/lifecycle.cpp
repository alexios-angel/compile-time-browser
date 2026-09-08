// browser - a page's lifecycle: loading a document (and the navigation a
// script queues from inside one), the reset between pages, and what an
// embedder installs on a browser - natives, fonts, hooks, a script to run.
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

void browser::use_renderer(renderer r) {
    renderer_ = std::move(r);
    mark(dirty::paint); // the new renderer has no tiles
}

void browser::load_html(std::string_view html) {
    load_document(html, source_kind::html);
}

void browser::load_document(std::string_view html, source_kind kind) {
    // A NAVIGATION FROM INSIDE A SCRIPT IS QUEUED, NOT PERFORMED. `element.click()`
    // reaches `browser::activate`, which calls the embedder's navigate hook, and
    // an embedder's hook calls this - ctbrowse's does - so this function can be
    // re-entered from inside a script that `run_scripts` is running.
    //
    // DOING IT IMMEDIATELY WAS A USE-AFTER-FREE, and one that predates giving
    // every <script> its own program: the nested load resets `script_` and
    // frees the programs, and then returns into an interpreter still executing
    // one of them, on a context that no longer exists. It also let the
    // abandoned page's remaining scripts run against the new document, read it
    // and overwrite it - measured, with the discarded page's second script
    // writing into a page that had already finished loading.
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
    // A queued navigation, and any it queues in turn. A page that navigates on
    // load forever is a page that hangs in a real browser too, so there is no
    // cap here that a real one does not also lack.
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
    source_html_ = html; // what location.reload() re-runs
    source_kind_ = kind;
    // Both the document and the cascade are rebuilt. Keeping the old style
    // engine would accumulate every page's <style> rules across navigations,
    // which shows up as the previous page bleeding into the next one.
    reset_document();
    // XML AND HTML ARE TWO FRONT ENDS, not one with a flag. See dom/xml.hpp:
    // nothing is implied, every tag may self-close, case is preserved, and a
    // `<script>` written as `<![CDATA[ ... ]]>` is code rather than code with
    // a marked section on the front. A malformed XML document does not
    // recover - there is nothing to recover to - so the error is kept and the
    // partial tree is loaded, which is what lets an embedder show it.
    xml_error_.clear();
    parse_result parsed;
    if (kind == source_kind::xml) {
        xml_parse_result read = parse_xml(*doc_, html);
        parsed = std::move(read.tree);
        xml_error_ = std::move(read.error);
    } else {
        parsed = parse_html(*doc_, html);
    }
    title_ = extract_title();
    scroll_y_ = 0;
    author_sheet_loaded_ = false;
    style_error_.clear();
    load_author_styles();
    // Images are resolved BEFORE layout, because an <img> with no width
    // attribute takes its size from the decoded bitmap and layout has no
    // way to ask. The page's @font-face files, for the same reason: layout
    // measures with them.
    load_images();
    // AFTER load_images, which clears the store before walking for <img>. An
    // inline <svg>'s source came from the parse rather than from a file, but
    // from here on the two are the same thing: a graphic to rasterise at
    // whatever size its box turns out to be.
    for (const auto & [id, source] : parsed.svg_sources) { svg_.set_source(id, source); }
    load_page_fonts();
    mark(dirty::everything);
    run_scripts();
    // AND THE PAGE HAS LOADED. Announced on the next tick, not here - see
    // browser::load_event_pending_ for why the delay is the point rather than
    // an accident.
    load_event_pending_ = true;
}

void browser::reset_document() {
    doc_ = std::make_unique<document>(atoms_);
    styles_ = std::make_unique<ctbrowser::style::engine>(atoms_);
    // BEFORE the first sheet, so its conditions are evaluated against the real
    // viewport rather than against the 1024x768 default and then corrected.
    (void)media_environment_changed();
    styles_->add_sheet(ctbrowser::style::ua_css, ctbrowser::style::ua_origin);
    // The engine object is NEW, so bindings that outlive this call would be
    // matching selectors through a freed one.
    if (bindings_) { bindings_->observe_style_engine(*styles_); }
    resolved_.clear();
}

void browser::reload() {
    const std::string source = source_html_;
    // by value: load_document clears source_html_'s referent
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

// WHERE THE VENDORED FACES ARE, and the reason it is a function rather than
// three lines inside use_real_fonts: a PACKAGER has to ask the same question.
// The faces are loaded through the asset registry under names that begin with
// this directory, so an application that bakes them in has to bake them under
// the names the run will ask for - and a packager that worked the directory out
// for itself would be a second copy of this rule, free to drift from the one
// that decides at run time.
std::string browser::default_font_directory() {
    const char * from_env = std::getenv("CTBROWSER_FONT_PATH");
    return from_env != nullptr ? std::string{from_env} : std::string{"fonts"};
}

bool browser::use_real_fonts(std::string_view directory) {
    // An empty directory means "ask the build". Resolved HERE rather than as a
    // default argument because a default argument cannot read the environment,
    // and resolved for every caller rather than at seventeen call sites
    // because the eighteenth would be the one that forgot.
    std::string resolved;
    if (directory.empty()) {
        resolved = default_font_directory();
        directory = resolved;
    }
#if CTBROWSER_WITH_TTF
    auto backend = std::make_unique<ctbrowser::raster::ttf_backend>();
    if (!backend->ok()) { return false; }
    // The baked-in faces first, if this build has any. They go into the same
    // registry the loop below reads, under the same names, so nothing after
    // this point knows or cares whether a face came from the binary or the
    // disk - which is what the registry-before-filesystem order was always
    // for. A build without them registers nothing and the loop reads the
    // directory, exactly as before.
    (void)register_embedded_fonts(assets_, directory);
    // family, then the four (bold, italic) files that make it up.
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
    backend->set_default_family("serif"); // what the UA sheet gives <body>
    ttf_ = std::move(backend);
    load_page_fonts();
    fonts_ = ttf_.get();
    renderer_.set_fonts(fonts_);
    // The canvas measures and draws its own text, so it needs the same backend
    // - otherwise a page's canvas keeps the bitmap font while the document
    // around it switches to real faces.
    canvases_.set_fonts(fonts_);
    // Everything measured so far was measured with the other font.
    mark(dirty::everything);
    return true;
#else
    (void)directory;
    return false;
#endif
}

void browser::allow_network(bool allowed) {
    network_allowed_ = allowed;
    if (bindings_) { bindings_->allow_network(allowed); }
}

bool browser::run_script(std::string_view source) {
    if (!script_) { return false; }
    auto compiled =
        std::make_unique<script::program>(script::compiler::compile(std::string{source}));
    if (!compiled->ok) {
        script_error_ = compiled->error;
        return false;
    }
    // KEPT, not scoped. A closure holds a `const function_proto *` into its
    // program, and anything that calls back later - a timer, a listener,
    // requestAnimationFrame - dereferences it long after this returned. A
    // temporary works exactly until the first callback.
    const script::program & kept = *compiled;
    extra_programs_.push_back(std::move(compiled));
    const script::run_result result = script_->run(kept);
    // CLEARED ON SUCCESS, not only set on failure. It was only ever assigned,
    // so one broken script made every later good one report that same error for
    // the rest of the page's life - and callers use this to decide whether the
    // script ran at all.
    script_error_ = result.ok ? std::string{} : result.error;
    // A SUCCESSFUL RUN DOES NOT ERASE A FAULT IT DID NOT CAUSE. Callbacks
    // report through the same field, and running any script afterwards - a
    // test's probe, a devtools eval - would otherwise clear the one message
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

void browser::set_navigate_hook(std::function<void(const std::string &)> hook) {
    navigate_hook_ = std::move(hook);
}

} // namespace ctbrowser::shell
