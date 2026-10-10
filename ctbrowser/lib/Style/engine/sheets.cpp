//===- ctbrowser/lib/Style/engine/sheets.cpp - Sheets -----------*- C++ -*-===//
//
// Stylesheet ingestion for style::engine: element state bits, parsing a sheet
// into the rule index, @layer/@scope/@container/@property/@function
// bookkeeping, layer ranking, scope-root search and container query
// evaluation. The declarations are documented in style/engine.hpp.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/style/engine.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>

#include <span>
#include <string>
#include <vector>

namespace ctbrowser::style {

bool engine::set_state(node_id id, std::uint32_t bits, bool on) {
    if (!id || bits == 0) { return false; }
    const std::uint64_t key = key_of(id);
    const auto it = states_.find(key);
    const std::uint32_t before = it == states_.end() ? 0u : it->second;
    const std::uint32_t after = on ? (before | bits) : (before & ~bits);
    if (after == before) { return false; }
    if (after == 0) {
        states_.erase(key);
    } else {
        states_[key] = after;
    }
    return true;
}

std::uint32_t engine::state_of(node_id id) const {
    if (states_source_ != nullptr) { return states_source_->state_of(id); }
    const auto it = states_.find(key_of(id));
    return it == states_.end() ? 0u : it->second;
}

void engine::clear_origin(std::uint8_t origin) {
    const auto drop = [origin](std::vector<rule> & rules) {
        std::erase_if(rules, [origin](const rule & r) { return r.origin == origin; });
    };
    for (auto & [key, rules] : index_.by_id) { drop(rules); }
    for (auto & [key, rules] : index_.by_class) { drop(rules); }
    for (auto & [key, rules] : index_.by_tag) { drop(rules); }
    drop(index_.universal);
    // The layer, scope and container tables are reset once no remaining rule (of
    // any origin) refers to them. A page that rewrites `@layer b, a` to
    // `@layer a, b` has reordered its layers, and a table that kept the old first
    // appearance would disagree.
    bool layered = false;
    bool scoped = false;
    const auto scan = [&](const std::vector<rule> & rules) {
        for (const rule & r : rules) {
            layered = layered || r.layer != 0;
            scoped = scoped || r.scope != 0;
        }
    };
    for (const auto & [key, rules] : index_.by_id) { scan(rules); }
    for (const auto & [key, rules] : index_.by_class) { scan(rules); }
    for (const auto & [key, rules] : index_.by_tag) { scan(rules); }
    scan(index_.universal);
    // An `@keyframes` of another origin also keeps the layer table alive, since
    // it is ranked by its layer too.
    std::erase_if(keyframes_, [origin](const keyframes_rule & k) { return k.origin == origin; });
    for (const keyframes_rule & k : keyframes_) { layered = layered || k.layer != 0; }
    if (!layered) {
        // Entry 0 is "unlayered", ranked last (0xFFFFFFFF) so it beats every
        // layer.
        layer_names_.assign(1, std::string{});
        layer_rank_.assign(1, 0xFFFFFFFFu);
    }
    if (!scoped) { scopes_.assign(1, scope_entry{}); }
    bool contained = false;
    const auto scan_containers = [&](const std::vector<rule> & rules) {
        for (const rule & r : rules) { contained = contained || r.container != 0; }
    };
    for (const auto & [key, rules] : index_.by_id) { scan_containers(rules); }
    for (const auto & [key, rules] : index_.by_class) { scan_containers(rules); }
    for (const auto & [key, rules] : index_.by_tag) { scan_containers(rules); }
    scan_containers(index_.universal);
    if (!contained) { containers_.assign(1, css::container_condition{}); }
    // A sheet's @function rules go with its other rules; a registration does not.
    erase_if(functions_, [origin](const auto & entry) { return entry.second.origin == origin; });
    // The @font-face list is not indexed by origin and is not cleared: a face is
    // a resource the browser has already been asked to load, and unloading one
    // because a rule was edited is a different question from unsaying the rule.
}

bool engine::register_property(std::string_view name, css::property_registration registration) {
    if (!name.starts_with("--")) { return false; }
    const atom key = atoms_->intern(name);
    if (registrations_.contains(key.id)) { return false; }
    // The initial value must be computationally independent or the registration
    // is invalid (CSS Properties and Values API 1 §2.1): random(), sibling-index(),
    // sibling-count() and var() give a different answer per element, and there is
    // no element here. Syntax `*` accepts anything (WPT random-computed).
    if (registration.syntax != "*") {
        const std::string_view initial = registration.initial;
        if (css::may_have_var(initial) || initial.find("random(") != std::string_view::npos ||
            initial.find("sibling-index(") != std::string_view::npos ||
            initial.find("sibling-count(") != std::string_view::npos) {
            return false;
        }
    }
    registrations_.emplace(key.id, std::move(registration));
    return true;
}

// Helpers for reading the @property and @function rules the parser
// collected. Both are declaration blocks with a prelude; the descriptors come
// back through `declarations_of` the way @font-face's do, and the prelude is a
// run of component values.
namespace {

/// The token at a run's front, or nullptr when the front is not a single token.
[[nodiscard]] const css::css_token * token_at(const css::stylesheet & sheet,
                                              std::span<const css::component_value> run) {
    if (run.empty() || run.front().kind != css::cv_kind::token) { return nullptr; }
    return &sheet.tokens[run.front().token];
}

/// A run's text: every token's text joined, which is what the tokenizer saw
/// minus the comments.
[[nodiscard]] std::string text_of_run(const css::stylesheet & sheet,
                                      std::span<const css::component_value> run) {
    std::string out;
    if (run.empty()) { return out; }
    for (std::uint32_t t = run.front().token; t < run.back().end_token; ++t) {
        out += sheet.text_of(sheet.tokens[t]);
    }
    return out;
}

/// The top-level pieces of `run` split at `separator` tokens (only the first
/// when `first_only`), each trimmed: the parameters at commas, a parameter or a
/// declaration at its colon. A separator inside a function or a block is a
/// child and invisible here.
[[nodiscard]] std::vector<std::span<const css::component_value>> split_at(
    const css::stylesheet & sheet, std::span<const css::component_value> run,
    css::token_type separator, bool first_only) {
    std::vector<std::span<const css::component_value>> pieces;
    std::size_t start = 0;
    for (std::size_t i = 0; i < run.size(); ++i) {
        const css::css_token * t = token_at(sheet, run.subspan(i));
        if (t != nullptr && t->type == separator && !(first_only && !pieces.empty())) {
            pieces.push_back(sheet.trimmed(run.subspan(start, i - start)));
            start = i + 1;
        }
    }
    pieces.push_back(sheet.trimmed(run.subspan(start)));
    return pieces;
}

/// A `<css-type>` as text: `<number>`, `<length>+`, or `type(<number> | auto)`
/// with the `type()` wrapper removed; an empty run is `*`.
[[nodiscard]] std::string syntax_of(const css::stylesheet & sheet,
                                    std::span<const css::component_value> run) {
    run = sheet.trimmed(run);
    if (run.empty()) { return "*"; }
    if (run.front().kind == css::cv_kind::function &&
        ascii_iequals(sheet.text_of(sheet.tokens[run.front().token]), "type(")) {
        return std::string{
            trim(text_of_run(sheet, sheet.children_of(run.front())), html_whitespace)};
    }
    return std::string{trim(text_of_run(sheet, run), html_whitespace)};
}

} // namespace

// `@property --x { syntax: "<length>"; inherits: true; initial-value: 3px }`,
// CSS Properties and Values API 1 §2: three descriptors in a block. A rule
// missing `syntax` or `inherits`, or `initial-value` for a syntax that is not
// `*`, registers nothing (§2.1 says it is invalid, as is a descriptor marked
// `!important`), and the first registration of a name is the one that stands.
void engine::register_at_property_rules(const css::stylesheet & sheet) {
    const atom syntax_key = atoms_->intern_lower("syntax");
    const atom inherits_key = atoms_->intern_lower("inherits");
    const atom initial_key = atoms_->intern_lower("initial-value");
    for (const css::at_rule_block & rule : sheet.properties) {
        // The prelude: one `--name` ident.
        const auto prelude = sheet.trimmed(sheet.prelude_of(rule));
        const css::css_token * name_token = token_at(sheet, prelude);
        if (prelude.size() != 1 || name_token == nullptr ||
            name_token->type != css::token_type::ident) {
            continue;
        }
        css::property_registration made;
        bool have_syntax = false;
        bool have_inherits = false;
        bool have_initial = false;
        for (const css::raw_declaration & d : sheet.declarations_of(rule)) {
            if (d.important) { continue; }
            const std::string_view value = sheet.text_of(d);
            if (d.property == syntax_key) {
                // A string, per §2.1; the quotes come off here.
                const auto values = sheet.values_of(d);
                const css::css_token * only = token_at(sheet, values);
                if (values.size() == 1 && only != nullptr &&
                    only->type == css::token_type::string) {
                    const std::string_view quoted = sheet.text_of(*only);
                    made.syntax =
                        std::string{trim(quoted.substr(1, quoted.size() - 2), html_whitespace)};
                    have_syntax = true;
                }
            } else if (d.property == inherits_key) {
                if (ascii_iequals(value, "true") || ascii_iequals(value, "false")) {
                    made.inherits = ascii_iequals(value, "true");
                    have_inherits = true;
                }
            } else if (d.property == initial_key) {
                made.initial = std::string{value};
                have_initial = true;
            }
        }
        if (!have_syntax || !have_inherits) { continue; }
        if (made.syntax != "*" && !have_initial) { continue; }
        (void)register_property(sheet.text_of(*name_token), std::move(made));
    }
}

// `@function --name(--a <type>: default, --b) returns <type> { --x: ...;
// result: ... }`, CSS Functions and Mixins 1 §2. A rule whose prelude is not
// one dashed function token, or whose parameter list does not parse,
// registers nothing; the first registration of a name stands. Nested rules
// in the body (`@media`, `@supports`) are not declarations and are dropped.
//
// ponytail: no conditional rules inside a body; add them when a page writes
// one, since the component values are all here.
void engine::register_at_function_rules(const css::stylesheet & sheet, std::uint8_t origin) {
    const atom result_key = atoms_->intern_lower("result");
    for (const css::at_rule_block & rule : sheet.functions) {
        // The prelude: `--name(` ... `)`, then `returns <type>`?
        auto prelude = sheet.trimmed(sheet.prelude_of(rule));
        if (prelude.empty() || prelude.front().kind != css::cv_kind::function) { continue; }
        const std::string_view head = sheet.text_of(sheet.tokens[prelude.front().token]);
        if (!head.starts_with("--")) { continue; }
        css::custom_function made;
        made.name = std::string{head.substr(0, head.size() - 1)};
        bool valid = true;
        const auto params = sheet.trimmed(sheet.children_of(prelude.front()));
        if (!params.empty()) {
            for (const auto piece : split_at(sheet, params, css::token_type::comma, false)) {
                css::custom_function::parameter param;
                const auto halves = split_at(sheet, piece, css::token_type::colon, true);
                const css::css_token * name = token_at(sheet, halves.front());
                if (name == nullptr || name->type != css::token_type::ident ||
                    !sheet.text_of(*name).starts_with("--")) {
                    valid = false;
                    break;
                }
                param.name = std::string{sheet.text_of(*name)};
                param.syntax = syntax_of(sheet, halves.front().subspan(1));
                if (halves.size() > 1) {
                    param.has_default = true;
                    param.initial = text_of_run(sheet, halves[1]);
                }
                made.parameters.push_back(std::move(param));
            }
        }
        if (!valid) { continue; }
        prelude = sheet.trimmed(prelude.subspan(1));
        if (!prelude.empty()) {
            const css::css_token * word = token_at(sheet, prelude);
            if (word == nullptr || word->type != css::token_type::ident ||
                !ascii_iequals(sheet.text_of(*word), "returns")) {
                continue;
            }
            made.returns = syntax_of(sheet, prelude.subspan(1));
        }
        // The body: the locals and `result`, in source order; anything else
        // in the block is not one of this function's declarations.
        for (const css::raw_declaration & d : sheet.declarations_of(rule)) {
            if (d.important) { continue; }
            if (d.custom) {
                made.body.emplace_back(std::string{atoms_->text(d.property)},
                                       std::string{sheet.text_of(d)});
            } else if (d.property == result_key) {
                made.body.emplace_back("result", std::string{sheet.text_of(d)});
            }
        }
        const atom key = atoms_->intern(made.name);
        if (functions_.contains(key.id)) { continue; }
        functions_.emplace(key.id, sheet_function{origin, std::move(made)});
    }
}

void engine::add_sheet(std::string_view css, std::uint8_t origin) {
    const css::stylesheet sheet = css::parse_stylesheet(css, *atoms_);
    for (const css::font_face & face : sheet.font_faces) {
        const auto value_of = [&](std::string_view property) -> std::string_view {
            const atom want = atoms_->intern_lower(property);
            for (const css::raw_declaration & d : sheet.declarations_of(face)) {
                if (d.property == want) { return sheet.text_of(d); }
            }
            return {};
        };
        // Read the URL from the tokens, not the reassembled text: an unquoted
        // `url(f.ttf)` is a single url token whose text is just `f.ttf`, while
        // `url("f.ttf")` is a function token plus a string (CSS Syntax 3 §4.3.6).
        // Looking at tokens handles both forms the same way.
        const auto src_url = [&]() -> std::string_view {
            const atom want = atoms_->intern_lower("src");
            for (const css::raw_declaration & d : sheet.declarations_of(face)) {
                if (d.property != want) { continue; }
                for (const css::component_value & v : sheet.values_of(d)) {
                    const css::css_token & first = sheet.tokens[v.token];
                    // Unquoted form: the token text is the URL. `unquoted` trims
                    // the spaces that `url( f.ttf )` keeps inside the token.
                    if (first.type == css::token_type::url) {
                        return unquoted(sheet.text_of(first));
                    }
                    // Quoted form: a `url(` function token with a string inside. A
                    // function token's text includes the `(`, so strip it before
                    // comparing the name.
                    std::string_view name = sheet.text_of(first);
                    if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
                    if (first.type == css::token_type::function && ascii_iequals(name, "url")) {
                        for (const css::component_value & arg : sheet.children_of(v)) {
                            const css::css_token & inner = sheet.tokens[arg.token];
                            if (inner.type == css::token_type::string) {
                                return unquoted(sheet.text_of(inner));
                            }
                        }
                    }
                }
                // A `src` with no url at all (`src: local(Fira)`) is not an
                // error; it is a face this engine cannot load.
                break;
            }
            return {};
        };

        page_font entry;
        // Unquoted here: `font-family: 'Press Start 2P'` arrives with its quotes
        // on, and registering the name as it comes back would file the face under
        // a name no element can ever ask for.
        entry.family = std::string{unquoted(value_of("font-family"))};
        entry.source = std::string{src_url()};
        const std::string_view weight = value_of("font-weight");
        entry.bold = weight == "bold" || weight == "700" || weight == "800" || weight == "900" ||
                     weight == "600";
        const std::string_view style = value_of("font-style");
        entry.italic = style == "italic" || style == "oblique";
        if (!entry.family.empty() && !entry.source.empty()) { fonts_.push_back(std::move(entry)); }
    }
    register_at_property_rules(sheet);
    register_at_function_rules(sheet, origin);

    // The sheet's conditions, remapped into the engine's table. A sheet numbers its
    // own `@media` blocks from 1; the engine holds every sheet's, so index 0 stays the
    // shared unconditional entry and the rest are offset. Their parent links are
    // remapped the same way, so nesting survives the move.
    const std::size_t condition_base = conditions_.size();
    for (std::size_t i = 1; i < sheet.conditions.size(); ++i) {
        css::media_condition moved = sheet.conditions[i];
        if (moved.parent != 0) {
            moved.parent = static_cast<std::uint32_t>(condition_base + moved.parent - 1);
        }
        conditions_.push_back(std::move(moved));
        condition_truth_.push_back(false);
    }
    const auto engine_condition = [&](std::uint32_t sheet_local) -> std::uint32_t {
        return sheet_local == 0 ? 0u : static_cast<std::uint32_t>(condition_base + sheet_local - 1);
    };
    // Evaluate what was just added, against the environment as it stands.
    for (std::size_t i = condition_base; i < conditions_.size(); ++i) {
        condition_truth_[i] = condition_holds(i);
    }

    // The sheet's layers, merged into the engine's by name: `@layer a` in two
    // sheets is one layer, first declared where it first appeared. An
    // anonymous layer (a segment starting with \x01) is nobody else's, so its
    // name is made unique to this sheet with a serial before the lookup.
    const std::string serial = std::to_string(++sheet_serial_);
    std::vector<std::uint16_t> layer_map(sheet.layers.size() + 1, 0);
    for (std::size_t i = 0; i < sheet.layers.size(); ++i) {
        std::string name;
        for (std::size_t at = 0; at <= sheet.layers[i].size();) {
            const std::size_t dot = sheet.layers[i].find('.', at);
            const std::size_t end = dot == std::string::npos ? sheet.layers[i].size() : dot;
            std::string_view segment = std::string_view{sheet.layers[i]}.substr(at, end - at);
            if (!name.empty()) { name += '.'; }
            if (segment.starts_with('\x01')) {
                name += '\x01' + serial + '_';
                segment.remove_prefix(1);
            }
            name += segment;
            at = end + 1;
        }
        auto found = std::ranges::find(layer_names_, name);
        if (found == layer_names_.end()) {
            layer_names_.push_back(std::move(name));
            found = layer_names_.end() - 1;
        }
        layer_map[i + 1] = static_cast<std::uint16_t>(found - layer_names_.begin());
    }
    if (!sheet.layers.empty()) { rerank_layers(); }
    // Its `@keyframes`, each in the engine's layer its block sat in.
    file_keyframes(sheet, origin, condition_base, layer_map);
    // Its scopes, appended: a scope belongs to its sheet.
    std::vector<std::uint16_t> scope_map(sheet.scopes.size() + 1, 0);
    for (std::size_t i = 0; i < sheet.scopes.size(); ++i) {
        const css::scope_block & s = sheet.scopes[i];
        scope_entry made;
        made.parent = scope_map[s.parent];
        made.roots.assign(sheet.roots_of(s).begin(), sheet.roots_of(s).end());
        made.limits.assign(sheet.limits_of(s).begin(), sheet.limits_of(s).end());
        made.implicit_root = s.implicit_root;
        scopes_.push_back(std::move(made));
        scope_map[i + 1] = static_cast<std::uint16_t>(scopes_.size() - 1);
    }

    // Its `@container` conditions, appended with their nesting remapped.
    std::vector<std::uint16_t> container_map(sheet.containers.size() + 1, 0);
    for (std::size_t i = 0; i < sheet.containers.size(); ++i) {
        css::container_condition made = sheet.containers[i];
        made.parent = container_map[made.parent];
        containers_.push_back(std::move(made));
        container_map[i + 1] = static_cast<std::uint16_t>(containers_.size() - 1);
    }

    // One compiled selector per selector, and one rule per (selector,
    // declaration): the selector push is outside the declaration loop, and a
    // selector that can never match is never pushed.
    for (const css::raw_rule & r : sheet.rules) {
        for (const compiled_selector & compiled : sheet.selectors_of(r)) {
            if (compiled.parts.empty() || compiled.parts.front().never_matches) { continue; }
            selectors_.push_back(compiled);
            const std::uint32_t sel_index = static_cast<std::uint32_t>(selectors_.size() - 1);

            for (const css::raw_declaration & d : sheet.declarations_of(r)) {
                // Shorthands are stored unexpanded: how many components
                // `border: var(--all)` has is unknown until var() is substituted,
                // which needs the element. They are expanded when applied, so their
                // longhands still land at the shorthand's position in source order.
                declarations_.push_back(declaration{d.property, std::string{sheet.text_of(d)}});
                rule filed;
                filed.selector = sel_index;
                filed.declaration = static_cast<std::uint32_t>(declarations_.size() - 1);
                filed.order = d.order;
                filed.condition = engine_condition(r.condition);
                filed.layer = layer_map[r.layer];
                filed.scope = scope_map[r.scope];
                filed.container = container_map[r.container];
                filed.origin = origin;
                filed.important = d.important;
                index_.add(selectors_[sel_index], filed);
            }
        }
    }
}

// The layer order (CSS Cascade 5 §6.4.3): layers sort by first appearance,
// nested layers grouped within their parent and before the parent's own
// rules, and unlayered rules after every layer. As paths (`a.b` is [a, a.b])
// that is a lexicographic compare on the shared prefix, the longer path
// earlier when one is a prefix of the other; the unlayered entry is the empty
// path, a prefix of everything, and so last.
void engine::rerank_layers() {
    std::vector<std::vector<std::uint32_t>> paths(layer_names_.size());
    for (std::size_t i = 1; i < layer_names_.size(); ++i) {
        const std::string & name = layer_names_[i];
        for (std::size_t dot = name.find('.');; dot = name.find('.', dot + 1)) {
            const std::string_view prefix =
                std::string_view{name}.substr(0, dot == std::string::npos ? name.size() : dot);
            const auto at = std::ranges::find(layer_names_, prefix);
            paths[i].push_back(static_cast<std::uint32_t>(at - layer_names_.begin()));
            if (dot == std::string::npos) { break; }
        }
    }
    std::vector<std::uint32_t> order(layer_names_.size() - 1);
    for (std::size_t i = 0; i < order.size(); ++i) { order[i] = static_cast<std::uint32_t>(i + 1); }
    std::ranges::sort(order, [&](std::uint32_t a, std::uint32_t b) {
        const auto & pa = paths[a];
        const auto & pb = paths[b];
        const std::size_t shared = std::min(pa.size(), pb.size());
        for (std::size_t i = 0; i < shared; ++i) {
            if (pa[i] != pb[i]) { return pa[i] < pb[i]; }
        }
        return pa.size() > pb.size();
    });
    layer_rank_.assign(layer_names_.size(), 0xFFFFFFFFu);
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        layer_rank_[order[rank]] = static_cast<std::uint32_t>(rank);
    }
}

bool engine::scope_root_for(const read_txn & txn, const ancestor_filter & ancestors,
                            std::uint16_t s, std::size_t depth, bool explicit_scope,
                            std::size_t & root_depth) {
    // `depth + 1` stands for "no outer root" throughout: the top scope's
    // `:scope` is the document's, and its candidates run to the tree's top.
    const std::size_t none = depth + 1;
    // Is the element at `d` a root of `e` whose scope holds the subject, with
    // the outer root at `outer` being what `:scope` names in `e`'s roots? A
    // limit and everything under it is out of scope, the limit itself included
    // (CSS Cascade 6 §3.1), so any element strictly below the root down to the
    // subject that matches one ends the candidacy.
    const auto is_root = [&](const scope_entry & e, std::size_t d, std::size_t outer) {
        scope_ = outer == none ? node_id{} : levels_[outer][path_[outer]].node;
        // An implicit scope's root is one element, named by its key.
        bool any = e.roots.empty() && e.implicit_root != 0 &&
                   key_of(levels_[d][path_[d]].node) == e.implicit_root;
        for (const compiled_selector & sel : e.roots) {
            if (matches_from(txn, ancestors, sel, d, path_[d])) {
                any = true;
                break;
            }
        }
        if (!any) { return false; }
        scope_ = levels_[d][path_[d]].node;
        for (std::size_t k = d + 1; k <= depth; ++k) {
            for (const compiled_selector & sel : e.limits) {
                if (matches_from(txn, ancestors, sel, k, path_[k])) { return false; }
            }
        }
        return true;
    };
    // Every root depth of scope `at` that holds the subject, nearest first
    // under each root of the enclosing scope. An implicit scope (`@scope { }`
    // with no prelude) is rooted at the parent of the sheet's owner node, which
    // the embedder passes in as css::scope_block::implicit_root; a scope with no
    // such key has no root and matches nothing.
    using visitor = std::function<void(std::size_t)>;
    const auto each_root = [&](auto && self, std::uint16_t at, const visitor & f) -> void {
        if (at == 0) {
            f(none);
            return;
        }
        const scope_entry & e = scopes_[at];
        if (e.roots.empty() && e.implicit_root == 0) { return; }
        self(self, e.parent, visitor{[&](std::size_t outer) {
                 for (std::size_t d = depth + 1; d-- > (outer == none ? 0 : outer);) {
                     if (is_root(e, d, outer)) { f(d); }
                 }
             }});
    };
    bool found = false;
    each_root(each_root, s, visitor{[&](std::size_t d) {
                  // The subject may be its own scoping root only when the rule's selector
                  // names it: `.a { }` inside `@scope (.a)` is `:where(:scope) .a`.
                  if (d == depth && !explicit_scope) { return; }
                  if (!found || d > root_depth) { root_depth = d; }
                  found = true;
              }});
    scope_ = node_id{};
    return found;
}

namespace {

/// Whether `text` is a style feature's range form (`a < b`, `a <= b < c`) and,
/// if so, whether it is well formed: every operand present, both comparisons of
/// a double range pointing the same way, and no `!`. Returns nullopt when there
/// is no top-level comparison at all.
[[nodiscard]] std::optional<bool> range_shape(std::string_view text) {
    const css::token_stream s = css::tokenize(text);
    std::vector<char> ops;
    bool operand = false, bang = false, missing = false;
    int depth = 0;
    for (std::size_t i = 0; i + 1 < s.tokens.size(); ++i) {
        const css::css_token & t = s.tokens[i];
        if (t.type == css::token_type::whitespace) { continue; }
        const std::string_view d = s.text_of(t);
        if (t.type == css::token_type::delim && d == "!") {
            bang = true;
            continue;
        }
        // `--x: a < b` is a declaration, whatever its value holds.
        if (depth == 0 && t.type == css::token_type::colon && ops.empty()) { return std::nullopt; }
        if (depth == 0 && t.type == css::token_type::delim && (d == "<" || d == ">" || d == "=")) {
            missing = missing || !operand;
            ops.push_back(d.front());
            if (d != "=" && css::is_delim_text(s, i + 1, '=')) { ++i; }
            operand = false;
            continue;
        }
        if (t.type == css::token_type::function || t.type == css::token_type::open_paren) {
            ++depth;
        }
        if (t.type == css::token_type::close_paren) { --depth; }
        operand = true;
    }
    if (ops.empty()) { return std::nullopt; }
    if (bang || missing || !operand || ops.size() > 2) { return false; }
    return ops.size() == 1 || (ops[0] == ops[1] && ops[0] != '=');
}

} // namespace

std::optional<engine::container_size> engine::container_size_of(node_id node) {
    for (const auto & [asked, answer] : container_answers_) {
        if (asked == node) { return answer; }
    }
    std::optional<container_size> answer =
        container_size_ ? container_size_(node) : std::optional<container_size>{};
    container_answers_.emplace_back(node, answer);
    return answer;
}

bool engine::container_holds(const read_txn & txn, std::uint16_t c, std::size_t depth) {
    const atom type_key = atoms_->intern_lower("container-type");
    const atom name_key = atoms_->intern_lower("container-name");
    for (; c != 0; c = static_cast<std::uint16_t>(containers_[c].parent)) {
        // A comma list holds when any of its conditions does, each choosing
        // its own container (CSS Conditional 5 §3).
        bool held = false;
        for (const css::container_query & q : containers_[c].queries) {
            if (container_query_holds(txn, q, depth, type_key, name_key)) {
                held = true;
                break;
            }
        }
        if (!held) { return false; }
    }
    return true;
}

bool engine::container_query_holds(const read_txn & txn, const css::container_query & e,
                                   std::size_t depth, atom type_key, atom name_key) {
    // Which ancestor is asked (CSS Conditional 5 §2.2): the nearest one
    // carrying the name, if one was asked, whose `container-type` can answer
    // every feature the query uses: a size container on each axis a size
    // feature reads, a scroll-state container for `scroll-state()`. A
    // `style()` query asks nothing of it: every element is a style container.
    // ponytail: the requirements are re-read per element per rule; cache them
    // on the condition if a page with many containers shows it.
    const css::container_needs needs = css::container_requirements(e.condition);
    // A feature no container has: no container can be asked.
    if (needs.unknown) { return false; }
    bool held = false;
    // A pseudo-element's originating element is its container candidate too.
    for (std::size_t d = depth + (pseudo_wanted_ != atom{} ? 1 : 0); d-- > 0;) {
        if (d >= chain_styles_.size() || !chain_styles_[d]) { continue; }
        const computed_style_ptr & style = chain_styles_[d];
        if (!e.name.empty()) {
            bool named = false;
            for (const std::string_view n : split_top_level(style->get(name_key), " \t\n")) {
                if (n == e.name) { named = true; }
            }
            if (!named) { continue; }
        }
        bool size = false, inline_size = false, scroll_state = false;
        for (const std::string_view word : split_top_level(style->get(type_key), " \t\n")) {
            size = size || word == "size";
            inline_size = inline_size || word == "inline-size";
            scroll_state = scroll_state || word == "scroll-state";
        }
        if ((needs.block_axis && !size) || (needs.inline_axis && !size && !inline_size) ||
            (needs.scroll_state && !scroll_state)) {
            continue;
        }
        // The content box as last laid out. The container is chosen by its
        // style alone: one with no box to contain (not laid out yet,
        // `display: none`, an inline box) is still the one asked, and its
        // sizes are unknown (WPT never-match-container).
        css::container_box box;
        if (size || inline_size || scroll_state) {
            const std::optional<container_size> laid = container_size_of(levels_[d][path_[d]].node);
            // A vertical container's inline axis is its height.
            box.vertical =
                style->get(atoms_->intern_lower("writing-mode")).starts_with("vertical") ||
                style->get(atoms_->intern_lower("writing-mode")).starts_with("sideways");
            if (laid && (size || (inline_size && !box.vertical))) { box.width = laid->width; }
            if (laid && (size || (inline_size && box.vertical))) { box.height = laid->height; }
            if (laid && scroll_state) { box.scrollable = laid->scrollable; }
        }
        const css::media_environment & env = environment_;
        // The container's font, which an `em` in the query measures.
        {
            const auto get = [&](std::string_view name) {
                return style->get(atoms_->intern_lower(name));
            };
            const float font_size =
                css::length_text_to_px(get("font-size"), font_context(16.0f)).value_or(16.0f);
            box.lengths =
                font_context(font_size, line_height_px(get("line-height"), font_size),
                             measure_ ? zero_advance_of(get("font-family"), get("font-weight"),
                                                        get("font-style"), font_size)
                                      : 0.0f);
            // Its place among its siblings, for sibling-index() and
            // sibling-count() (WPT sibling-function-container-query).
            box.lengths.sibling_index = levels_[d][path_[d]].facts.sibling_index;
            box.lengths.sibling_count = levels_[d][path_[d]].facts.sibling_count;
            // Its `cq*` units measure the containers of the container, as
            // they would on the container itself (WPT
            // container-units-in-at-container).
            box.lengths.container_width = box.lengths.container_height = -1.0f;
            if (d > 0 && d - 1 < cq_chain_.size()) {
                const auto [inline_container, block_container] = cq_chain_[d - 1];
                if (inline_container) {
                    if (const auto up = container_size_of(inline_container)) {
                        box.lengths.container_width = up->width;
                    }
                }
                if (block_container) {
                    if (const auto up = container_size_of(block_container)) {
                        box.lengths.container_height = up->height;
                    }
                }
            }
        }
        // `style(...)` is the if() condition of the same name, asked of the
        // container rather than of the element (CSS Conditional 5 §3.3): its
        // custom properties, its attributes for an `attr()` in a range, its
        // font for an `em`. So it goes through the substituter that answers
        // `if(style(...): ...)` (plain, boolean and range features, nested
        // `and`/`or`/`not`), and a third branch keeps unknown apart from
        // false, since `not` of unknown is still unknown.
        const node_id at = levels_[d][path_[d]].node;
        // A random() in a query (csswg-drafts#10982), written in it or reached
        // through a custom property, makes the feature unknown.
        const auto random = [](std::string_view v) {
            return v.find("random(") != std::string_view::npos ||
                   v.find("random-item(") != std::string_view::npos;
        };
        const css::style_query query = [&](std::string_view text) {
            if (random(text)) { return css::truth::unknown; }
            const css::custom_lookup lookup = [&style,
                                               &random](atom n) -> std::optional<std::string_view> {
                const std::string_view v = style->get(n);
                if (v.empty() || v == guaranteed_invalid || random(v)) { return std::nullopt; }
                return v;
            };
            const css::attribute_lookup attributes =
                [&txn, at, this](std::string_view name) -> std::optional<std::string> {
                const atom key = atoms_->intern(name);
                if (!txn.has_attribute(at, key)) { return std::nullopt; }
                return std::string{txn.attribute_value(at, key)};
            };
            css::condition_environment conditions;
            conditions.computed = [&style,
                                   this](std::string_view name) -> std::optional<std::string> {
                const std::string_view v = style->get(atoms_->intern(name));
                if (v.empty() || v == guaranteed_invalid) { return std::nullopt; }
                return std::string{v};
            };
            conditions.media = [this](std::string_view t) {
                return css::evaluate_media_condition(t, environment_);
            };
            conditions.registered = [this](std::string_view name) {
                return registration_of(atoms_->intern(name));
            };
            conditions.functions = [this](std::string_view name) {
                return function_of(atoms_->intern(name));
            };
            // A registered <color> compares as a computed colour, under the
            // container's used scheme: `light-dark(white, black)` is black
            // in a dark container (WPT registered-color-style-queries).
            conditions.canonical_color = [&](std::string_view v) {
                bool light = false, dark = false;
                for (const std::string_view word :
                     split_top_level(style->get(atoms_->intern_lower("color-scheme")), " ")) {
                    light = light || word == "light";
                    dark = dark || word == "dark";
                }
                css::color_context colors;
                colors.dark = dark && !light;
                const std::string computed = css::computed_color(v, colors);
                return computed.empty() ? std::string{v} : computed;
            };
            // `inherit` in a query names the container's parent's value.
            conditions.inherited = [&](std::string_view name) -> std::optional<std::string> {
                if (d == 0 || d - 1 >= chain_styles_.size() || !chain_styles_[d - 1]) {
                    return std::nullopt;
                }
                const std::string_view v = chain_styles_[d - 1]->get(atoms_->intern(name));
                if (v.empty() || v == guaranteed_invalid) { return std::nullopt; }
                return std::string{v};
            };
            conditions.lengths = box.lengths;
            // A declaration's `!important` is no part of the value it asks
            // about (WPT custom-property-style-queries): `style(--foo: bar
            // !important)` asks for `bar`.
            std::string asked{text};
            if (const std::size_t bang = asked.rfind('!');
                bang != std::string::npos && asked.find(':') < bang &&
                ascii_iequals(trim(std::string_view{asked}.substr(bang + 1), html_whitespace),
                              "important")) {
                asked.resize(bang);
            }
            // Kept in a named string because `word` below views it.
            const std::string answer =
                css::substitute_var("if(style(" + asked + "): y; not style(" + asked +
                                        "): n; else: u)",
                                    lookup, *atoms_, attributes, &conditions)
                    .value_or("u");
            const std::string_view word = trim(answer, html_whitespace);
            // A malformed range is unknown whatever the if() made of it.
            const std::optional<bool> range = range_shape(text);
            if (range == false) { return css::truth::unknown; }
            if (word == "y") { return css::truth::yes; }
            // A well-formed range answers: two values that cannot be compared
            // are not in range (WPT at-container-style-parsing); only a range
            // with a `!` or its comparisons pointing both ways is unknown.
            return word == "n" || range == true ? css::truth::no : css::truth::unknown;
        };
        // A var() in the query reads the container's custom property (WPT
        // size-query-with-var): `(width > var(--w, 100px))`. One that fails to
        // substitute leaves the query false.
        std::string substituted;
        std::string_view condition = e.condition;
        // Skip substitution when the query contains random() or random-item():
        // both are substitution functions, and resolving them here would hide them
        // from the query, which must treat them as unknown (WPT
        // random-item-in-container-query).
        if (css::may_have_var(condition) && !random(condition)) {
            const css::custom_lookup lookup = [&style](atom n) -> std::optional<std::string_view> {
                const std::string_view v = style->get(n);
                if (v.empty() || v == guaranteed_invalid) { return std::nullopt; }
                return v;
            };
            std::optional<std::string> done = css::substitute_var(condition, lookup, *atoms_);
            if (!done) { break; }
            substituted = std::move(*done);
            condition = substituted;
        }
        // A name alone asks only that there be such a container.
        held = condition.empty() || css::evaluate_container_condition(condition, env, box, query)
                                            .value_or(css::truth::unknown) == css::truth::yes;
        break; // The nearest eligible container decides (§2.2).
    }
    return held;
}

} // namespace ctbrowser::style
