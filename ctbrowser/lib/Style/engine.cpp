#include <ctbrowser/style/engine.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>

#include <span>
#include <string>
#include <vector>

// engine: the method bodies.
// The header says what these do; this says how.

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
    // THE LAYER ORDER AND THE SCOPES GO WITH THE RULES that named them, once
    // no rule of any origin still points at one: a page that rewrites its
    // sheet from `@layer b, a` to `@layer a, b` has reordered its layers, and
    // a table that remembered the old first appearance would say otherwise.
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
    if (!layered) {
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
    std::erase_if(keyframes_, [origin](const keyframes_rule & k) { return k.origin == origin; });
    // The @font-face list is not indexed by origin and is not cleared: a face is
    // a resource the browser has already been asked to load, and unloading one
    // because a rule was edited is a different question from unsaying the rule.
}

bool engine::register_property(std::string_view name, css::property_registration registration) {
    if (!name.starts_with("--")) { return false; }
    const atom key = atoms_->intern(name);
    if (registrations_.contains(key.id)) { return false; }
    // THE INITIAL VALUE IS COMPUTATIONALLY INDEPENDENT or the registration is
    // invalid (CSS Properties and Values API 1 §2.1): one that draws a
    // `random()`, counts siblings or substitutes has a different answer per
    // element, and there is no element here. `*` takes anything
    // (random-computed).
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

// Reading the at-rules the parser collected. Both are declaration blocks
// with a prelude; the descriptors come back through `declarations_of` the
// way @font-face's do, and the prelude is a run of component values.
namespace {

// The token at a run's front, when the front IS a single token.
[[nodiscard]] const css::css_token * token_at(const css::stylesheet & sheet,
                                              std::span<const css::component_value> run) {
    if (run.empty() || run.front().kind != css::cv_kind::token) { return nullptr; }
    return &sheet.tokens[run.front().token];
}

// A run's text: every token's text joined, which is what the tokenizer saw
// minus the comments.
[[nodiscard]] std::string text_of_run(const css::stylesheet & sheet,
                                      std::span<const css::component_value> run) {
    std::string out;
    if (run.empty()) { return out; }
    for (std::uint32_t t = run.front().token; t < run.back().end_token; ++t) {
        out += sheet.text_of(sheet.tokens[t]);
    }
    return out;
}

// The top-level pieces of `run` split at `separator` tokens, each trimmed:
// the parameters at commas, a parameter or a declaration at its colon. A
// separator inside a function or a block is a child and invisible here.
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

// A `<css-type>`: `<number>`, `<length>+`, or `type(<number> | auto)`, whose
// wrapper comes off; nothing at all is `*`.
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
// `*`, registers nothing - §2.1 says it is invalid, as is a descriptor marked
// `!important` - and the first registration of a name is the one that stands.
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
// one - the component values are all here.
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
        // THE URL OUT OF THE TOKENS, not out of the text.
        //
        // `url(` WITH AN UNQUOTED BODY IS ITS OWN TOKEN and a quoted one is a
        // function plus a string - §4.3.6, and token.hpp says so on the
        // enumerator. So the two spellings do not look alike by the time a
        // declaration's text is reassembled: `url("f.ttf")` comes back with its
        // `url(` intact, while `url(f.ttf)` comes back as bare `f.ttf` with
        // nothing left to recognise it by. Asking the tokens instead makes the
        // two forms one case.
        const auto src_url = [&]() -> std::string_view {
            const atom want = atoms_->intern_lower("src");
            for (const css::raw_declaration & d : sheet.declarations_of(face)) {
                if (d.property != want) { continue; }
                for (const css::component_value & v : sheet.values_of(d)) {
                    const css::css_token & first = sheet.tokens[v.token];
                    // The unquoted form: the token's text IS the url, already
                    // unwrapped by the tokenizer.
                    // TRIMMED: `url( f.ttf )` keeps the whitespace inside the
                    // parens in the token's text, and a source with a trailing
                    // space is a file name nothing can open. `unquoted` trims
                    // and strips quotes; a url token's body is unquoted by
                    // definition, so here it only trims.
                    if (first.type == css::token_type::url) {
                        return unquoted(sheet.text_of(first));
                    }
                    // The quoted form: `url` as a function, with the string
                    // inside. A FUNCTION TOKEN'S TEXT CARRIES ITS `(` - the
                    // token is the ident and the paren together, which is what
                    // makes it a function rather than an ident - so the name is
                    // `url(` and comparing it to `url` never matches.
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
                // A `src` with no url at all - `src: local(Fira)` - is not an
                // error, it is a face this engine cannot load.
                break;
            }
            return {};
        };

        page_font entry;
        // UNQUOTED here: `font-family: 'Press Start 2P'` arrives with its quotes
        // on, so registering the name as it comes back files the face under a name
        // no element can ever ask for.
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

    // THE SHEET'S CONDITIONS, remapped into the engine's table. A sheet numbers its
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
    file_keyframes(sheet, origin, condition_base);

    // THE SHEET'S LAYERS, merged into the engine's by NAME: `@layer a` in two
    // sheets is one layer, first declared where it first appeared. An
    // anonymous layer is nobody else's, so its unnameable name is made unique
    // to this sheet before the lookup.
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
    // ...AND ITS SCOPES, appended: a scope belongs to its sheet.
    std::vector<std::uint16_t> scope_map(sheet.scopes.size() + 1, 0);
    for (std::size_t i = 0; i < sheet.scopes.size(); ++i) {
        const css::scope_block & s = sheet.scopes[i];
        scope_entry made;
        made.parent = scope_map[s.parent];
        made.roots.assign(sheet.roots_of(s).begin(), sheet.roots_of(s).end());
        made.limits.assign(sheet.limits_of(s).begin(), sheet.limits_of(s).end());
        scopes_.push_back(std::move(made));
        scope_map[i + 1] = static_cast<std::uint16_t>(scopes_.size() - 1);
    }

    // ...AND ITS `@container` CONDITIONS, appended with their nesting remapped.
    std::vector<std::uint16_t> container_map(sheet.containers.size() + 1, 0);
    for (std::size_t i = 0; i < sheet.containers.size(); ++i) {
        css::container_condition made = sheet.containers[i];
        made.parent = container_map[made.parent];
        containers_.push_back(std::move(made));
        container_map[i + 1] = static_cast<std::uint16_t>(containers_.size() - 1);
    }

    // ONE COMPILED SELECTOR PER SELECTOR, and one rule per (selector,
    // declaration): the push is outside the declaration loop, and a selector
    // that can never match is never pushed.
    for (const css::raw_rule & r : sheet.rules) {
        for (const compiled_selector & compiled : sheet.selectors_of(r)) {
            if (compiled.parts.empty() || compiled.parts.front().never_matches) { continue; }
            selectors_.push_back(compiled);
            const std::uint32_t sel_index = static_cast<std::uint32_t>(selectors_.size() - 1);

            for (const css::raw_declaration & d : sheet.declarations_of(r)) {
                // NOT EXPANDED HERE, and that is the point of the two-pass cascade. A
                // shorthand's component count is unknowable before var() substitution -
                // `border: var(--all)` is ONE token that becomes three - and
                // substitution needs the element. So the shorthand is recorded whole
                // and expanded when it is applied, which also keeps its source order:
                // its longhands land at the shorthand's position in the fold.
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

// THE LAYER ORDER, CSS Cascade 5 §6.4.3: layers sort by first appearance,
// nested layers grouped within their parent and BEFORE the parent's own
// rules, and unlayered rules after every layer. As paths - `a.b` is [a, a.b]
// - that is a lexicographic compare on the shared prefix, the longer path
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
    // LIMIT and everything under it is out of scope, the limit itself included
    // (CSS Cascade 6 §3.1), so any element strictly below the root down to the
    // subject that matches one ends the candidacy.
    const auto is_root = [&](const scope_entry & e, std::size_t d, std::size_t outer) {
        scope_ = outer == none ? node_id{} : levels_[outer][path_[outer]].node;
        bool any = false;
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
    // under each root of the enclosing scope. An implicit scope - `@scope { }`
    // with no prelude - is rooted at the sheet's owner node's parent, which
    // this engine is not told; it has no roots here and matches nothing.
    using visitor = std::function<void(std::size_t)>;
    const auto each_root = [&](auto && self, std::uint16_t at, const visitor & f) -> void {
        if (at == 0) {
            f(none);
            return;
        }
        const scope_entry & e = scopes_[at];
        if (e.roots.empty()) { return; }
        self(self, e.parent, visitor{[&](std::size_t outer) {
                 for (std::size_t d = depth + 1; d-- > (outer == none ? 0 : outer);) {
                     if (is_root(e, d, outer)) { f(d); }
                 }
             }});
    };
    bool found = false;
    each_root(each_root, s, visitor{[&](std::size_t d) {
                  // The subject may be its own scoping root only when the rule's selector
                  // names it - `.a { }` inside `@scope (.a)` is `:where(:scope) .a`.
                  if (d == depth && !explicit_scope) { return; }
                  if (!found || d > root_depth) { root_depth = d; }
                  found = true;
              }});
    scope_ = node_id{};
    return found;
}

bool engine::container_holds(const read_txn & /*txn*/, std::uint16_t c, std::size_t depth) {
    const atom type_key = atoms_->intern_lower("container-type");
    const atom name_key = atoms_->intern_lower("container-name");
    for (; c != 0; c = static_cast<std::uint16_t>(containers_[c].parent)) {
        const css::container_condition & e = containers_[c];
        // A size query needs a size container; a `style()`-only query takes
        // any ancestor (CSS Containment 3 §5.1).
        const bool wants_size = e.condition.find('(') != std::string::npos &&
                                e.condition.find("style(") == std::string::npos;
        bool held = false;
        for (std::size_t d = depth; d-- > 0;) {
            if (d >= chain_styles_.size() || !chain_styles_[d]) { continue; }
            const computed_style_ptr & style = chain_styles_[d];
            if (!e.name.empty()) {
                bool named = false;
                for (const std::string_view n : split_top_level(style->get(name_key), " \t\n")) {
                    if (n == e.name) { named = true; }
                }
                if (!named) { continue; }
            }
            css::media_environment env = environment_;
            if (wants_size) {
                const std::string_view type = style->get(type_key);
                const bool sized = type.find("size") != std::string_view::npos;
                if (!sized) { continue; }
                const std::optional<container_size> box =
                    container_size_ ? container_size_(levels_[d][path_[d]].node) : std::nullopt;
                if (!box) { return false; }
                env.viewport_width = box->width;
                env.viewport_height = box->height;
            }
            // `style(--x: y)`: the container's computed value of the property
            // against the query's, as text (custom properties are token
            // streams, CSS Containment 3 §5.3); `style(--x)` alone asks whether
            // it has a value other than the guaranteed-invalid one.
            const css::style_query query = [&](std::string_view text) {
                const std::size_t colon = text.find(':');
                const std::string_view name = trim(text.substr(0, colon), html_whitespace);
                if (name.empty()) { return css::truth::unknown; }
                const std::string_view have = style->get(atoms_->intern(name));
                if (colon == std::string_view::npos) {
                    return !have.empty() && have != guaranteed_invalid ? css::truth::yes
                                                                       : css::truth::no;
                }
                const std::string_view want = trim(text.substr(colon + 1), html_whitespace);
                return trim(have, html_whitespace) == want ? css::truth::yes : css::truth::no;
            };
            held = css::evaluate_container_condition(e.condition, env, query)
                       .value_or(css::truth::unknown) == css::truth::yes;
            break; // the nearest eligible container decides, §5.1
        }
        if (!held) { return false; }
    }
    return true;
}

// THE DOCUMENT ELEMENT HAS NO SIBLINGS. Depth 0 is entered from the root
// itself - its level totals are its CHILDREN's - so the counts the traversal
// wrote for `<html>` were its children's: `:only-child` and `:last-of-type`
// were false of it (child-indexed-pseudo-class). A parentless element is one
// of one, as cursor_to already has it.
void engine::root_facts(const read_txn & txn, node_id node, std::size_t depth,
                        element_facts & facts) const {
    if (depth != 0 || txn.parent(node)) { return; }
    facts.sibling_index = facts.sibling_count = facts.type_index = facts.type_count = 1;
}

element_facts engine::facts_of(const read_txn & txn, node_id id) const {
    element_facts f;
    f.tag = txn.tag(id).value_or(atom{});
    const std::string_view id_attr = txn.attribute_value(id, id_name());
    if (!id_attr.empty()) { f.id = atoms_->intern(id_attr); }
    split_classes(txn.attribute_value(id, class_name()), f.classes);
    f.states = state_of(id);
    // `:focus-visible` is `:focus` here (selector.hpp says why), and
    // `:focus-within` asks whether the focused element - there is at most a
    // handful of elements with any state at all - is this one or under it.
    if ((f.states & state_focus) != 0) { f.states |= state_focus_visible | state_focus_within; }
    for (const auto & [key, bits] : (states_source_ ? states_source_ : this)->states_) {
        if ((bits & state_focus) == 0 || (f.states & state_focus_within) != 0) { continue; }
        const node_id focused{static_cast<std::uint32_t>(key >> 32),
                              static_cast<std::uint32_t>(key & 0xFFFFFFFFu)};
        if (txn.is_ancestor_of(id, focused)) { f.states |= state_focus_within; }
    }
    // The document element: THE TREE'S ROOT, which is <html> itself here - there
    // is no Document node above it. Not "no element parent": a fragment's
    // top-level children and a detached element have none either, and Selectors
    // 4 §14.1 makes `:root` the document's root element alone -
    // `fragment.querySelectorAll(":root")` asserts the miss.
    f.is_root = id == txn.root();
    // The form-control facts. `disabled` is an attribute, so `:disabled` is a
    // question about the document rather than about UI state.
    const std::string_view tag_text = atoms_->text(f.tag);
    f.can_be_disabled = tag_text == "button" || tag_text == "input" || tag_text == "select" ||
                        tag_text == "textarea" || tag_text == "optgroup" || tag_text == "option" ||
                        tag_text == "fieldset";
    f.is_disabled = f.can_be_disabled && txn.has_attribute(id, atoms_->intern("disabled"));
    f.is_checked =
        txn.has_attribute(id, atoms_->intern("checked")) || (f.states & state_checked) != 0;
    // `a` and `area` ONLY, HTML §4.16.2: a `<link href>` used to be one, and the spec
    // moved it out - `#head :link` in ParentNode-querySelector-All.html asserts the miss.
    f.is_link =
        (tag_text == "a" || tag_text == "area") && txn.has_attribute(id, atoms_->intern("href"));
    // `:empty` - no element children and no text. WHITESPACE COUNTS as content per
    // the spec, so `<p> </p>` is not empty; a comment does not.
    f.is_empty = true;
    for (const node_id child : txn.children(id)) {
        const node_kind kind = txn.kind(child).value_or(node_kind::comment);
        if (kind == node_kind::element) {
            f.is_empty = false;
            break;
        }
        if (is_text_kind(kind) && !txn.text(child).empty()) {
            f.is_empty = false;
            break;
        }
    }
    return f;
}

style_map engine::resolve_all(const read_txn & txn) {
    style_map out;
    ancestor_filter ancestors;
    // CLEARED, not merely reused. levels_ persists across calls so its capacity
    // does, but its CONTENTS are the previous traversal's elements - and depth 0
    // accumulates, so a second resolve would find the old document's <html> sitting
    // before the new one and `html ~ x` would match across two documents. clear()
    // on the inner vectors keeps the capacity that makes the reuse worth having.
    for (std::vector<visited_element> & level : levels_) { level.clear(); }
    // DEPTH 0 HAS NO ELEMENT PARENT to set it up. Every other level is entered by
    // the element whose children occupy it; the document element's level is entered
    // here, from the document node - which is what gives <html> a sibling count and
    // makes `:only-child` true of it.
    enter_level(txn, txn.root(), 0);
    scope_ = {}; // a stylesheet's `:scope` is `:root`
    resolve_subtree(txn, txn.root(), ancestors, out);
    return out;
}

std::vector<node_id> engine::select(const read_txn & txn, node_id root,
                                    std::span<const compiled_selector> list, bool first_only,
                                    node_id scope) {
    std::vector<node_id> found;
    if (list.empty()) { return found; }
    // The cascade's traversal state, reset exactly as resolve_all resets it: the
    // capacity is worth keeping across calls and the CONTENTS are the last walk's
    // elements, which would make `html ~ x` match across two documents.
    for (std::vector<visited_element> & level : levels_) { level.clear(); }
    ancestor_filter ancestors;
    // The top of the tree `root` is in. For a connected root that is the document
    // element; for a DETACHED one - `box.innerHTML = ...; box.querySelector(s)` -
    // it is the subtree's own top, which a walk from the document would never
    // reach.
    node_id top = root ? root : txn.root();
    while (const node_id up = txn.parent(top)) { top = up; }
    enter_level(txn, top, 0);
    scope_ = scope ? scope : root;

    // ONLY THE PATH TO THE ROOT IS WALKED ABOVE IT. Every element on the way down
    // is visited - a sibling combinator needs the earlier siblings at each level -
    // but a subtree that does not contain the root holds nothing the query can
    // answer with, and descending into it made `el.querySelectorAll` cost the
    // whole document. `:has()` runs one of these per subject, and paid that
    // per element.
    const auto toward_root = [&](node_id node) { return root && txn.is_ancestor_of(node, root); };
    // Returns false to unwind the whole walk, which is how first_only stops.
    const auto walk = [&](auto && self, node_id node, std::size_t depth, bool collect) -> bool {
        if (txn.kind(node).value_or(node_kind::text) != node_kind::element) {
            // A non-element does not occupy a depth - see resolve_subtree, which has
            // to agree with this or `+` would mean two different things. It can
            // still BE the root - a ShadowRoot is a fragment - and its children
            // are the descendants a subtree search collects.
            const bool below = collect || node == root;
            if (!below && !toward_root(node)) { return true; }
            for (const node_id child : txn.children(node)) {
                if (!self(self, child, depth, below)) { return false; }
            }
            return true;
        }
        if (levels_.size() <= depth) { levels_.resize(depth + 1); }
        if (path_.size() <= depth) { path_.resize(depth + 1); }
        if (totals_.size() <= depth) { totals_.resize(depth + 1); }
        element_facts my_facts = facts_of(txn, node);
        my_facts.sibling_index = static_cast<std::uint32_t>(levels_[depth].size()) + 1;
        my_facts.sibling_count = totals_[depth].elements;
        my_facts.type_index = totals_[depth].next_for(my_facts.tag);
        my_facts.type_count = totals_[depth].total_for(my_facts.tag);
        root_facts(txn, node, depth, my_facts);
        levels_[depth].push_back(visited_element{node, std::move(my_facts)});
        path_[depth] = levels_[depth].size() - 1;

        bool keep_going = true;
        if (collect) {
            for (const compiled_selector & sel : list) {
                if (!matches(txn, ancestors, sel, depth)) { continue; }
                found.push_back(node);
                keep_going = !first_only;
                break;
            }
        }
        // A subtree search collects from BELOW the root, never the root itself:
        // `element.querySelectorAll(s)` is over descendants.
        const bool below = collect || node == root;
        if (keep_going && (below || toward_root(node))) {
            // Read back from levels_ rather than from `my_facts`: matches() may have
            // grown the vector and moved it, exactly as resolve_subtree warns.
            const visited_element & me = levels_[depth][path_[depth]];
            const atom my_tag = me.facts.tag;
            const atom my_id = me.facts.id;
            const boost::container::small_vector<atom, 4> my_classes = me.facts.classes;
            ancestors.push(my_tag, my_id, my_classes);
            enter_level(txn, node, depth + 1);
            for (const node_id child : txn.children(node)) {
                if (!self(self, child, depth + 1, below)) {
                    keep_going = false;
                    break;
                }
            }
            ancestors.pop(my_tag, my_id, my_classes);
        }
        return keep_going;
    };
    // An empty root is the whole document, and the document's own root element is
    // one of the answers - there is no Document node above <html> in this tree.
    (void)walk(walk, top, 0, !root);
    return found;
}

bool engine::element_matches(const read_txn & txn, node_id node,
                             std::span<const compiled_selector> list, node_id scope) {
    if (list.empty()) { return false; }
    scope_ = scope ? scope : node;
    ancestor_filter ancestors;
    const std::optional<std::size_t> at = cursor_to(txn, node, ancestors);
    if (!at) { return false; }
    for (const compiled_selector & sel : list) {
        if (matches(txn, ancestors, sel, *at)) { return true; }
    }
    return false;
}

node_id engine::closest(const read_txn & txn, node_id subject,
                        std::span<const compiled_selector> list) {
    if (list.empty()) { return {}; }
    for (node_id at = subject; at; at = txn.parent(at)) {
        if (element_matches(txn, at, list, subject)) { return at; }
    }
    return {};
}

computed_style_ptr engine::resolve_pseudo(const read_txn & txn, node_id node, atom pseudo,
                                          const computed_style_ptr & element) {
    scope_ = node_id{};
    ancestor_filter ancestors;
    const std::optional<std::size_t> at = cursor_to(txn, node, ancestors);
    if (!at) { return {}; }
    pseudo_wanted_ = pseudo;
    const element_facts & self = levels_[*at][path_[*at]].facts;
    computed_style_ptr out = resolve(txn, node, self, ancestors, *at, element);
    pseudo_wanted_ = atom{};
    return out;
}

std::optional<std::size_t> engine::cursor_to(const read_txn & txn, node_id node,
                                             ancestor_filter & ancestors) {
    if (txn.kind(node).value_or(node_kind::text) != node_kind::element) { return std::nullopt; }
    // The element chain from the document down to `node`. Only elements occupy a
    // depth, exactly as resolve_subtree has it, or `+` would mean two things.
    std::vector<node_id> chain;
    for (node_id at = node; at; at = txn.parent(at)) {
        if (txn.kind(at).value_or(node_kind::text) == node_kind::element) { chain.push_back(at); }
    }
    if (chain.empty()) { return std::nullopt; }
    std::ranges::reverse(chain);

    for (std::vector<visited_element> & level : levels_) { level.clear(); }
    for (std::size_t depth = 0; depth < chain.size(); ++depth) {
        if (levels_.size() <= depth) { levels_.resize(depth + 1); }
        if (path_.size() <= depth) { path_.resize(depth + 1); }
        if (totals_.size() <= depth) { totals_.resize(depth + 1); }
        // The parent whose children occupy this level. Depth 0 is entered from
        // `chain[0]`'s OWN parent - NOT `txn.root()`, because a DETACHED element
        // is a different tree: `document.createElement("div").matches("div")`
        // must not be answered about <body>.
        const node_id parent = depth == 0 ? txn.parent(chain[0]) : chain[depth - 1];
        if (!parent) {
            // A PARENTLESS SUBJECT IS AN ONLY CHILD. There is no level to walk,
            // so it is built by hand: one element, index 1 of 1, which is what
            // `:only-child` and `:first-child` correctly answer for a node that
            // is in no tree at all.
            if (depth != 0) { return std::nullopt; } // chain[depth-1] is always an element
            levels_[depth].clear();
            totals_[depth] = level_totals{};
            element_facts facts = facts_of(txn, chain[0]);
            facts.sibling_index = 1;
            facts.sibling_count = 1;
            facts.type_index = 1;
            facts.type_count = 1;
            levels_[depth].push_back(visited_element{chain[0], std::move(facts)});
            path_[depth] = 0;
            if (depth + 1 < chain.size()) {
                const element_facts & mine = levels_[depth][path_[depth]].facts;
                ancestors.push(mine.tag, mine.id, mine.classes);
            }
            continue;
        }
        enter_level(txn, parent, depth);
        // EVERY EARLIER SIBLING, because `.a + .b` and `.a ~ .b` look backwards
        // and there is no previous-sibling link to walk. Later ones are never
        // read, so the loop stops at the chain element.
        for (const node_id child : txn.children(parent)) {
            if (txn.kind(child).value_or(node_kind::text) != node_kind::element) { continue; }
            element_facts facts = facts_of(txn, child);
            facts.sibling_index = static_cast<std::uint32_t>(levels_[depth].size()) + 1;
            facts.sibling_count = totals_[depth].elements;
            facts.type_index = totals_[depth].next_for(facts.tag);
            facts.type_count = totals_[depth].total_for(facts.tag);
            levels_[depth].push_back(visited_element{child, std::move(facts)});
            if (child == chain[depth]) { break; }
        }
        if (levels_[depth].empty()) { return std::nullopt; } // the chain left the tree
        path_[depth] = levels_[depth].size() - 1;
        // The filter holds the SUBJECT's ancestors and not the subject itself.
        if (depth + 1 < chain.size()) {
            const element_facts & mine = levels_[depth][path_[depth]].facts;
            ancestors.push(mine.tag, mine.id, mine.classes);
        }
    }
    return chain.size() - 1;
}

const engine::inline_block & engine::inline_style_of(const read_txn & txn, node_id id) {
    static const inline_block none;
    const std::string_view text = txn.attribute_value(id, style_name());
    if (text.empty()) { return none; }
    const auto cached = inline_cache_.find(text);
    if (cached != inline_cache_.end()) { return cached->second; }

    // A declaration list directly, no `*{...}` wrap: `!important` survives, and a
    // `}` inside an attribute value cannot end a dummy rule early.
    inline_block parsed;
    const css::stylesheet sheet = css::parse_declaration_list(text, *atoms_);
    for (const css::raw_declaration & d : sheet.declarations) {
        auto & into = d.important ? parsed.important : parsed.normal;
        // Not expanded here either - the cascade does it, after substitution.
        into.push_back(declaration{d.property, std::string{sheet.text_of(d)}});
    }
    return inline_cache_.emplace(std::string{text}, std::move(parsed)).first->second;
}

std::string_view engine::unquoted(std::string_view text) {
    text = trim(text, " \t");
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
        text.back() == text.front()) {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

void engine::split_classes(std::string_view list,
                           boost::container::small_vector<atom, 4> & out) const {
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && (list[i] == ' ' || list[i] == '\t' || list[i] == '\n')) { ++i; }
        const std::size_t start = i;
        while (i < list.size() && list[i] != ' ' && list[i] != '\t' && list[i] != '\n') { ++i; }
        if (i > start) { out.push_back(atoms_->intern(list.substr(start, i - start))); }
    }
}

namespace {

// One `[name op value]` requirement against one element's attribute value.
//
// ORDERED CHEAPEST FIRST inside the compound below: the tag, the id and the classes
// are interned atoms and compare as integers, so an attribute - which reads the
// element's attribute list and then compares strings - is tested last.
[[nodiscard]] bool attribute_matches(std::string_view have, const attribute_match & want) {
    const auto same = [&](std::string_view a, std::string_view b) {
        return want.case_insensitive ? ascii_iequals(a, b) : a == b;
    };
    switch (want.op) {
    case attr_op::present: return true; // the caller has already established it exists
    case attr_op::exact: return same(have, want.value);
    case attr_op::includes: {
        // A whitespace-separated list. An EMPTY value can never be one of the
        // items, because the items are non-empty by construction.
        if (want.value.empty()) { return false; }
        std::size_t at = 0;
        while (at < have.size()) {
            const std::size_t start = have.find_first_not_of(html_whitespace, at);
            if (start == std::string_view::npos) { break; }
            std::size_t end = have.find_first_of(html_whitespace, start);
            if (end == std::string_view::npos) { end = have.size(); }
            if (same(have.substr(start, end - start), want.value)) { return true; }
            at = end;
        }
        return false;
    }
    case attr_op::dash:
        // `[lang|=en]` matches `en` and `en-GB` but not `english`. The hyphen form
        // is the whole point - it is how a language subtag is selected.
        if (same(have, want.value)) { return true; }
        return have.size() > want.value.size() && have[want.value.size()] == '-' &&
               same(have.substr(0, want.value.size()), want.value);
    // The three substring forms match NOTHING when the value is empty, per the
    // spec. Without that, `[a^=""]` would match every element with the attribute,
    // since every string starts with the empty string.
    case attr_op::prefix:
        return !want.value.empty() && have.size() >= want.value.size() &&
               same(have.substr(0, want.value.size()), want.value);
    case attr_op::suffix:
        return !want.value.empty() && have.size() >= want.value.size() &&
               same(have.substr(have.size() - want.value.size()), want.value);
    case attr_op::substring: {
        if (want.value.empty()) { return false; }
        if (!want.case_insensitive) { return have.find(want.value) != std::string_view::npos; }
        if (want.value.size() > have.size()) { return false; }
        for (std::size_t at = 0; at + want.value.size() <= have.size(); ++at) {
            if (ascii_iequals(have.substr(at, want.value.size()), want.value)) { return true; }
        }
        return false;
    }
    }
    return false;
}

// The structural pseudo-classes. Every one is arithmetic on four numbers the
// traversal already counted, which is the point of counting them there.
//
// `:root` is the document element - the one with no ELEMENT parent - which is
// <html> for every page this engine loads.
[[nodiscard]] bool structural_matches(const element_facts & f, std::uint32_t want) {
    if ((want & structural_root) != 0 && !f.is_root) { return false; }
    if ((want & structural_empty) != 0 && !f.is_empty) { return false; }
    if ((want & structural_first_child) != 0 && f.sibling_index != 1) { return false; }
    if ((want & structural_last_child) != 0 && f.sibling_index != f.sibling_count) { return false; }
    if ((want & structural_only_child) != 0 && f.sibling_count != 1) { return false; }
    if ((want & structural_first_of_type) != 0 && f.type_index != 1) { return false; }
    if ((want & structural_last_of_type) != 0 && f.type_index != f.type_count) { return false; }
    if ((want & structural_only_of_type) != 0 && f.type_count != 1) { return false; }
    if ((want & structural_disabled) != 0 && !f.is_disabled) { return false; }
    // `:enabled` is NOT the negation of `:disabled`: it applies only to elements that
    // could be disabled, so it is false of a <div> rather than true.
    if ((want & structural_enabled) != 0 && (!f.can_be_disabled || f.is_disabled)) { return false; }
    if ((want & structural_checked) != 0 && !f.is_checked) { return false; }
    if ((want & structural_link) != 0 && !f.is_link) { return false; }
    if ((want & structural_visited) != 0) { return false; } // always, on purpose
    return true;
}

// A VALID CUSTOM ELEMENT NAME, HTML §4.13.2 (the ASCII subset). Contains a
// hyphen, starts with an ASCII lowercase letter, holds only PCEN characters,
// and is not one of the eight reserved SVG/MathML names. An element whose local
// name is one - or a customized built-in whose `is=` value is one - has custom
// element state "undefined" until it is defined, which is the only case `:defined`
// can rule out from the tree alone. Bytes >= 0x80 pass so a Unicode name is not
// mistaken for a built-in; uppercase ASCII does not, so `is="Foo"` is uncustomized.
[[nodiscard]] bool is_potential_custom_element_name(std::string_view name) {
    if (name.empty() || name[0] < 'a' || name[0] > 'z') { return false; }
    if (name.find('-') == std::string_view::npos) { return false; }
    for (const char ch : name) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
                        ch == '.' || ch == '_' || static_cast<unsigned char>(ch) >= 0x80;
        if (!ok) { return false; }
    }
    static constexpr std::string_view reserved[] = {
        "annotation-xml", "color-profile",    "font-face",      "font-face-src",
        "font-face-uri",  "font-face-format", "font-face-name", "missing-glyph"};
    for (const std::string_view r : reserved) {
        if (name == r) { return false; }
    }
    return true;
}

// `An+B`: does `index` appear in the series for some non-negative n?
//
// The spec's series is An+B for n = 0, 1, 2, ..., and only POSITIVE results count -
// an index is one-based. The two degenerate cases are the ones to get right: a == 0
// is a single index rather than a series, and a negative step counts DOWN from B, so
// `:nth-child(-n+3)` is the first three.
[[nodiscard]] bool nth_matches(std::int32_t a, std::int32_t b, std::uint32_t index_u) {
    const auto index = static_cast<std::int32_t>(index_u);
    if (index <= 0) { return false; }
    if (a == 0) { return index == b; }
    const std::int32_t offset = index - b;
    if (offset % a != 0) { return false; }
    return offset / a >= 0;
}

} // namespace

bool engine::compound_matches(const read_txn & txn, const ancestor_filter & ancestors,
                              const compound & c, std::size_t depth, std::size_t index) const {
    if (c.never_matches) { return false; }
    const visited_element & subject = levels_[depth][index];
    const node_id node = subject.node;
    const element_facts & f = subject.facts;
    // A NAME FOLDS ONLY AGAINST AN HTML ELEMENT. Selectors 4 §6.1: a type or
    // attribute name is matched ASCII case-insensitively in an HTML document, and
    // that rule is about the ELEMENT's namespace rather than the document's. This
    // tokenizer preserves case inside foreign content - which is what makes the
    // spec's ~95 adjustment tables unnecessary here - so folding unconditionally
    // meant no selector could ever name `linearGradient` or `[viewBox]`.
    const node_ns ns = txn.element_ns(node);
    const bool folds = ns == node_ns::html;
    if (c.tag && (folds ? c.tag : c.tag_exact) != f.tag) { return false; }
    // THE NAMESPACE, when the selector names one: `svg|*` and, under a default
    // `@namespace`, every unprefixed compound. The DOM keeps an element's
    // namespace as html, svg or other, so those are the two URIs that match.
    if (c.ns_uri) {
        const std::string_view uri = atoms_->text(c.ns_uri);
        const bool fits = (ns == node_ns::html && uri == "http://www.w3.org/1999/xhtml") ||
                          (ns == node_ns::svg && uri == "http://www.w3.org/2000/svg");
        if (!fits) { return false; }
    }
    if (c.id && c.id != f.id) { return false; }
    for (const atom want : c.classes) {
        if (std::ranges::find(f.classes, want) == f.classes.end()) { return false; }
    }
    if ((c.states & f.states) != c.states) { return false; }
    // STRUCTURAL requirements, all answered from facts the traversal gathered.
    if (c.structural != 0 && !structural_matches(f, c.structural)) { return false; }
    if ((c.structural & structural_scope) != 0 && (scope_ ? node != scope_ : !f.is_root)) {
        return false;
    }
    // `:defined`. The honest subset the tree can answer: a non-HTML element is
    // uncustomized and always defined; an HTML element is defined unless its own
    // name is a potential custom element name (an autonomous custom element,
    // undefined until upgraded) or it carries an `is=` that names one (a
    // customized built-in, likewise). An element already upgraded reads as
    // undefined here, because the registry that would say otherwise lives in the
    // shell - see selector.hpp's structural_defined.
    if ((c.structural & structural_defined) != 0) {
        bool defined = true;
        if (ns == node_ns::html) {
            if (is_potential_custom_element_name(atoms_->text(f.tag))) {
                defined = false;
            } else {
                const std::string_view is_attr = txn.attribute_value(node, atoms_->intern("is"));
                if (is_potential_custom_element_name(is_attr)) { defined = false; }
            }
        }
        if (!defined) { return false; }
    }
    // ATTRIBUTES: everything above compares interned integers, and this reads the
    // element's attribute list and then compares strings.
    for (const attribute_match & want : c.attributes) {
        const atom name = folds ? want.name : want.name_exact;
        if (want.ns == ns_prefix::unset) {
            if (!txn.has_attribute(node, name)) { return false; }
            if (want.op == attr_op::present) { continue; }
            if (!attribute_matches(txn.attribute_value(node, name), want)) { return false; }
            continue;
        }
        // A NAMESPACED attribute selector is a question about the LOCAL name and
        // the namespace the DOM stored, so it walks the element's attributes: an
        // element may hold `title` in no namespace and `title` in another, and
        // `[*|title]` is satisfied by whichever of them matches the value.
        const std::string_view local_want = atoms_->text(name);
        bool held = false;
        for (const attribute & have : txn.attributes(node)) {
            const bool ns_fits = want.ns == ns_prefix::any ||
                                 (want.ns == ns_prefix::none ? !have.ns : have.ns == want.ns_uri);
            if (!ns_fits) { continue; }
            const std::string_view local = attribute_local_name(*atoms_, have);
            if (!(folds ? ascii_iequals(local, local_want) : local == local_want)) { continue; }
            if (want.op == attr_op::present || attribute_matches(have.value, want)) {
                held = true;
                break;
            }
        }
        if (!held) { return false; }
    }
    // AND THE ARGUMENT-CARRYING PSEUDO-CLASSES LAST OF ALL, because a nested
    // selector list runs the matcher again - possibly with combinators of its own.
    for (const pseudo_ref & want : c.pseudos) {
        switch (want.kind) {
        case pseudo_kind::nth_child:
            if (!nth_matches(want.a, want.b, f.sibling_index)) { return false; }
            break;
        case pseudo_kind::nth_last_child:
            if (!nth_matches(want.a, want.b, f.sibling_count + 1 - f.sibling_index)) {
                return false;
            }
            break;
        case pseudo_kind::nth_of_type:
            if (!nth_matches(want.a, want.b, f.type_index)) { return false; }
            break;
        case pseudo_kind::nth_last_of_type:
            if (!nth_matches(want.a, want.b, f.type_count + 1 - f.type_index)) { return false; }
            break;
        case pseudo_kind::not_:
        case pseudo_kind::is_:
        case pseudo_kind::where_: {
            if (want.relative) {
                // `:has()`: does any argument - `:scope > .a`, `:scope ~ .b` -
                // match something with this element as the scope? A scoped query
                // from the subject finds a descendant; a sibling argument needs
                // the search to start one level up, and the argument's own
                // combinator says which. The walker is a second engine because
                // this one is mid-traversal - see has_walker_.
                if (!has_walker_) {
                    has_walker_ = std::make_unique<engine>(*atoms_);
                    has_walker_->states_source_ = states_source_ ? states_source_ : this;
                }
                bool any = false;
                for (const compiled_selector & arg : want.args) {
                    const bool sideways =
                        !arg.links.empty() && (arg.links.back() == combinator::next_sibling ||
                                               arg.links.back() == combinator::subsequent_sibling);
                    const node_id parent = txn.parent(node);
                    const node_id from = sideways && parent ? parent : node;
                    const std::span<const compiled_selector> one{&arg, 1};
                    // ponytail: the argument's anchor and its `:scope` are one
                    // compound, so inside a scoped rule `:scope` here is the
                    // subject rather than the scoping root (Cascade 6 §3.3);
                    // split the anchor into a bit of its own when a page needs it.
                    if (!has_walker_->select(txn, from, one, true, node).empty()) {
                        any = true;
                        break;
                    }
                }
                if (!any) { return false; }
                break;
            }
            // A nested selector's SUBJECT is this element, so each argument is run
            // from the same cursor the outer selector is at. `:is()` and `:where()`
            // pass if any argument matches; `:not()` passes only if none does.
            bool any = false;
            for (const compiled_selector & arg : want.args) {
                if (matches_from(txn, ancestors, arg, depth, index)) {
                    any = true;
                    break;
                }
            }
            if (want.kind == pseudo_kind::not_ ? any : !any) { return false; }
            break;
        }
        // Both are answered from an ANCESTOR's attribute rather than from this
        // element's facts, so both walk - see language_of and direction_is_rtl,
        // which is where the whole of the rule lives.
        case pseudo_kind::lang: {
            const std::string_view have = language_of(txn, node);
            bool any = false;
            for (const std::string & range : want.ranges) {
                if (language_matches(range, have)) {
                    any = true;
                    break;
                }
            }
            if (!any) { return false; }
            break;
        }
        case pseudo_kind::dir: {
            if (want.ranges.size() != 1) { return false; }
            const bool rtl = direction_is_rtl(txn, node);
            if (want.ranges.front() != (rtl ? "rtl" : "ltr")) { return false; }
            break;
        }
        case pseudo_kind::heading: {
            // An HTML h1-h6, its level the digit; the list, when there is
            // one, has to name it.
            if (txn.element_ns(node) != node_ns::html) { return false; }
            const std::string_view local = txn.local_name(node);
            if (local.size() != 2 || local[0] != 'h' || local[1] < '1' || local[1] > '6') {
                return false;
            }
            const std::int32_t level = local[1] - '0';
            if (!want.levels.empty() &&
                std::ranges::find(want.levels, level) == want.levels.end()) {
                return false;
            }
            break;
        }
        }
    }
    return true;
}

computed_style_ptr engine::resolve(const read_txn & txn, node_id node, const element_facts & self,
                                   const ancestor_filter & ancestors, std::size_t depth,
                                   const computed_style_ptr & parent) {
    // THE PARENT'S WHOLE STYLE, not just its inherited half, and the difference is
    // `inherit` itself: the keyword takes the parent's value for ANY property,
    // inherited or not, so `display: inherit` has to be able to read a property
    // that never travels on its own. The inherited half alone cannot answer that.
    const inherited_ptr & from = parent ? parent->inherited : no_inherited_;
    // Where this element sits among its siblings, for `sibling-index()` and
    // `sibling-count()`: facts the traversal already gathered for
    // `:nth-child`, handed to every math function this element folds.
    sibling_index_ = self.sibling_index;
    sibling_count_ = self.sibling_count;
    element_key_ = key_of(node);
    // Gather only the rules whose RIGHTMOST compound could possibly match.
    matches_.clear();
    collect(index_.by_id, self.id, txn, ancestors, depth);
    for (const atom c : self.classes) { collect(index_.by_class, c, txn, ancestors, depth); }
    collect(index_.by_tag, self.tag, txn, ancestors, depth);
    for (const rule & r : index_.universal) { consider(r, txn, ancestors, depth); }

    // THE CASCADE, CSS Cascade 5 §6.1: importance, then origin - reversed for
    // important declarations, where the user agent's beat the author's - then
    // the layer, whose order is likewise reversed for important declarations
    // (§6.4), then specificity, then scope proximity (Cascade 6 §6.3, nearer
    // wins), then source order. Sorting ascending and applying in order means
    // the last write to a property wins, which is exactly the rule.
    std::ranges::stable_sort(matches_, [this](const rule & a, const rule & b) {
        if (a.important != b.important) { return !a.important; }
        if (a.origin != b.origin) {
            return a.important ? a.origin > b.origin : a.origin < b.origin;
        }
        if (a.layer != b.layer) {
            const std::uint32_t ra = layer_rank_[a.layer];
            const std::uint32_t rb = layer_rank_[b.layer];
            return a.important ? ra > rb : ra < rb;
        }
        const specificity sa = selectors_[a.selector].spec;
        const specificity sb = selectors_[b.selector].spec;
        if (sa != sb) { return sa < sb; }
        if (a.proximity != b.proximity) { return a.proximity > b.proximity; }
        return a.order < b.order;
    });

    declaration_list out;
    // Applying a declaration means REPLACING the property if it is already
    // there - the later write wins, which is what "the cascade" reduces to
    // once the sort has put everything in priority order.
    //
    // THE EXPLICIT-DEFAULTING KEYWORDS are resolved here, because here is where
    // the parent's value is in hand:
    //
    //   inherit   take the parent's value, whether or not the property inherits
    //   initial   an EMPTY value, which shadows anything inherited and reads as
    //             "nothing said" to every consumer - the closest thing to a real
    //             initial value until the property table carries them
    //   unset     drop the declaration: for an inherited property the inherited
    //             value then shows through, and for a non-inherited one absence
    //             already means initial, so dropping is correct for both
    //   revert    the value the PREVIOUS ORIGIN would have produced
    //   revert-layer, revert-rule
    //             likewise, one layer or one rule back - see rolled_back
    //
    // THE ROLLBACKS ARE ANSWERED FROM THE SORTED MATCHES rather than from
    // intermediate states of the fold: the value with some set of
    // declarations removed is the last remaining declaration of the property
    // in cascade order, and the matches are already in that order. `folding`
    // is the position in it of the rule being applied, or the size for the
    // style attribute, which every rule precedes.
    std::size_t folding = matches_.size();
    // The last declaration of `property` before `limit` that `keep` admits,
    // or the size when there is none.
    const auto rolled_back = [this](atom property, std::size_t limit, const auto & keep) {
        for (std::size_t i = limit; i-- > 0;) {
            const rule & r = matches_[i];
            if (keep(r) && declarations_[r.declaration].property == property) { return i; }
        }
        return matches_.size();
    };
    const auto put = [&out, &parent, &folding, &rolled_back, this](const declaration & d) {
        std::string value = d.value;
        const std::string_view property = atoms_->text(d.property);
        // THE ROLLBACK KEYWORDS, each the cascade with some declarations
        // struck out (CSS Cascade 5 §7.3): `revert` strikes the author origin,
        // so the answer is the last user-agent declaration - the whole list,
        // since important UA rules sort after the author's; `revert-layer`
        // strikes the current layer of the current origin, the style
        // attribute counting as a layer above all of them; `revert-rule` (a
        // Cascade 6 draft) strikes the current rule - the declarations filed
        // consecutively under one selector. What is found may be a keyword
        // itself, so this loops, always to an earlier position.
        std::size_t at = folding;
        for (int guard = 0; guard < 16; ++guard) {
            std::size_t found = matches_.size();
            if (value == "revert") {
                found = rolled_back(d.property, matches_.size(),
                                    [](const rule & r) { return r.origin == 0; });
            } else if (value == "revert-layer") {
                const rule * self = at < matches_.size() ? &matches_[at] : nullptr;
                found = rolled_back(d.property, at, [self](const rule & r) {
                    return self == nullptr || r.origin != self->origin || r.layer != self->layer;
                });
            } else if (value == "revert-rule") {
                const rule * self = at < matches_.size() ? &matches_[at] : nullptr;
                // add_sheet files one selector copy per block, so the same
                // selector index is the same block.
                found = rolled_back(d.property, at, [self](const rule & r) {
                    return self == nullptr || r.selector != self->selector;
                });
            } else {
                break;
            }
            if (found == matches_.size()) {
                value = "unset";
                break;
            }
            at = found;
            value = declarations_[matches_[found].declaration].value;
        }
        if (value == "inherit") {
            value = std::string{parent ? parent->get(d.property) : std::string_view{}};
        } else if (value == "unset" || value == "revert") {
            for (std::size_t i = 0; i < out.size(); ++i) {
                if (out[i].property == d.property) {
                    out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            if (!inherits(property)) { return; }
            // An inherited property must be actively removed from the own half so
            // the inherited value shows through; it already is.
            return;
        } else if (value == "initial") {
            // ON A CUSTOM PROPERTY `initial` IS THE GUARANTEED-INVALID VALUE,
            // not an empty one: `--bs-table-bg-type: initial` is a SENTINEL that
            // `var(--bs-table-bg-type, <fallback>)` has to fall through, where an
            // empty custom property - `--bs-btn-font-family: ;` - is a valid
            // empty substitution.
            //
            // The declaration is KEPT, holding the sentinel, rather than
            // erased: erasing it would let an ancestor's value show through,
            // and `initial` means invalid HERE regardless of what was
            // inherited.
            if (property.starts_with("--")) {
                value = std::string{guaranteed_invalid};
            } else {
                value.clear();
            }
        }
        for (declaration & existing : out) {
            if (existing.property == d.property) {
                existing.value = std::move(value);
                return;
            }
        }
        out.push_back(declaration{d.property, std::move(value)});
    };

    // The style ATTRIBUTE. Not a separate origin: it is author-level with a
    // specificity above every selector, so it lands between the normal
    // declarations and the important ones. Chrome and Firefox both give
    //
    //   normal selector  <  normal inline  <  important selector  <
    //   important inline
    //
    // which is why this is spliced into the fold at the importance
    // boundary rather than simply appended at the end - `!important` in a
    // stylesheet has to be able to beat a style attribute.
    // ...AND NOT FOR A PSEUDO-ELEMENT, which has no style attribute.
    static const inline_block no_inline;
    const inline_block & own = pseudo_wanted_ ? no_inline : inline_style_of(txn, node);

    // TWO PASSES, and the reason is that custom properties are themselves
    // cascaded: substitution cannot run inside the fold that produces the values
    // it needs to read. So pass one applies ONLY custom properties, and pass two
    // substitutes everything else against them.
    //
    // Both passes walk the same sorted list with the same inline-style splice, so
    // priority is identical between them - and expansion happening in pass two
    // keeps source order for free: a shorthand's longhands land at the shorthand's
    // position in the fold, so a longhand written after it still wins and one
    // written before it is still overwritten. Expansion cannot happen earlier,
    // when a rule is recorded, because a shorthand's component count is
    // unknowable before substitution: `border: var(--w) solid var(--c)` cannot be
    // split into longhands until the var()s are gone.
    const auto fold = [&](const auto & apply) {
        bool spliced = false;
        for (std::size_t i = 0; i < matches_.size(); ++i) {
            const rule & r = matches_[i];
            if (r.important && !spliced) {
                folding = matches_.size();
                for (const declaration & d : own.normal) { apply(d); }
                spliced = true;
            }
            folding = i;
            apply(declarations_[r.declaration]);
        }
        folding = matches_.size();
        if (!spliced) {
            for (const declaration & d : own.normal) { apply(d); }
        }
        for (const declaration & d : own.important) { apply(d); }
    };

    // PASS ONE: the custom properties, stored verbatim. A custom property's value
    // is never parsed and never validated - it is a token stream that means
    // whatever the var() reading it makes of it.
    //
    // ...EXCEPT FOR AN `inherit()` IN ONE, which is replaced NOW (CSS Values
    // 5 §inherit-notation): it names the PARENT's computed value, and the
    // parent's cascade has just produced it - a lazy reading later would
    // have to re-run the parent's substitution in the parent's scope, and
    // its grandparent's, which is how `--v: e2 inherit(--v)` under `--v:
    // e1` failed to accumulate (inherit-function-basic). A var() in the
    // same value stays lazy, as every var() in a custom property is.
    fold([&](const declaration & d) {
        if (!atoms_->text(d.property).starts_with("--")) { return; }
        if (d.value.find("inherit(") == std::string::npos) {
            put(d);
            return;
        }
        const css::token_stream s = css::tokenize(d.value);
        std::string value;
        for (std::size_t i = 0; i + 1 < s.tokens.size(); ++i) {
            const css::css_token & t = s.tokens[i];
            if (t.type != css::token_type::function || !ascii_iequals(s.text_of(t), "inherit(")) {
                value += s.text_of(t);
                continue;
            }
            const std::size_t close = css::end_of_block(s, i);
            const std::size_t last =
                s.tokens[close - 1].type == css::token_type::close_paren ? close - 1 : close;
            std::string inner;
            for (std::size_t v = i + 1; v < last; ++v) { inner += s.text_of(s.tokens[v]); }
            const std::size_t comma = inner.find(',');
            const std::string_view name =
                trim(std::string_view{inner}.substr(0, comma), html_whitespace);
            const std::string_view held =
                parent && name.starts_with("--") ? parent->get(atoms_->intern(name)) : "";
            if (!held.empty() && held != guaranteed_invalid) {
                value += held;
            } else if (comma != std::string::npos) {
                value += trim(std::string_view{inner}.substr(comma + 1), html_whitespace);
            } else {
                value = std::string{guaranteed_invalid};
                break;
            }
            i = close - 1;
        }
        put(declaration{d.property, std::move(value)});
    });

    // `nullopt` means NOT DEFINED, which is what makes `var()` take its
    // fallback; an empty string means defined and empty, which substitutes to
    // nothing. The guaranteed-invalid sentinel reads as the first of those,
    // which is exactly what CSS Variables §3 says `initial` does to a custom
    // property.
    const auto lookup = [&out, &parent](atom name) -> std::optional<std::string_view> {
        const auto answer = [](std::string_view held) -> std::optional<std::string_view> {
            if (held == guaranteed_invalid) { return std::nullopt; }
            return held;
        };
        for (const declaration & d : out) {
            if (d.property == name) { return answer(d.value); }
        }
        if (parent && parent->inherited) {
            for (const declaration & d : parent->inherited->declarations) {
                if (d.property == name) { return answer(d.value); }
            }
        }
        return std::nullopt;
    };
    // ...AND THE ELEMENT'S ATTRIBUTES, for `attr()`. Absent and empty are
    // different answers here too: `attr(data-x)` on an element without the
    // attribute takes its fallback, and one with `data-x=""` is `""`.
    const css::attribute_lookup attributes =
        [&txn, node, this](std::string_view name) -> std::optional<std::string> {
        const atom key = atoms_->intern(name);
        if (!txn.has_attribute(node, key)) { return std::nullopt; }
        return std::string{txn.attribute_value(node, key)};
    };
    // ...AND WHAT `if()` MAY ASK (CSS Values 5 §if-notation): the parent's
    // custom properties for `style(--x: inherit)`, substituted against the
    // parent's own scope; any property folded so far for `style(color:
    // green)`; and the window for `media()`. The bases are filled in below
    // once the font size is known, and the property per declaration.
    css::condition_environment conditions;
    conditions.inherited = [&parent, this](std::string_view name) -> std::optional<std::string> {
        if (!parent) { return std::nullopt; }
        // `get` reads the parent's own half too, where a registered
        // property that does not inherit lives.
        const std::string_view held = parent->get(atoms_->intern(name));
        if (held.empty() || held == guaranteed_invalid) { return std::nullopt; }
        const css::custom_lookup above = [&parent](atom n) -> std::optional<std::string_view> {
            const std::string_view v = parent->get(n);
            if (v.empty() || v == guaranteed_invalid) { return std::nullopt; }
            return v;
        };
        return css::substitute_var(held, above, *atoms_);
    };
    conditions.computed = [&out, &parent,
                           this](std::string_view name) -> std::optional<std::string> {
        const atom key = atoms_->intern(name);
        for (const declaration & d : out) {
            if (d.property == key) { return d.value; }
        }
        if (parent && parent->inherited) {
            const std::string_view held = parent->inherited->get(key);
            if (!held.empty()) { return std::string{held}; }
        }
        return std::nullopt;
    };
    conditions.media = [this](std::string_view text) {
        return css::evaluate_media_condition(text, environment_);
    };
    conditions.registered = [this](std::string_view name) {
        return registration_of(atoms_->intern(name));
    };
    conditions.functions = [this](std::string_view name) {
        return function_of(atoms_->intern(name));
    };

    // PASS ONE AND A HALF: FONT SIZE, ALONE, BEFORE ANYTHING ELSE READS IT.
    //
    // `em` means the element's own font size on every property except font-size
    // itself, where it means the parent's - so the one value everything else is
    // relative to has to be known before the general fold runs. That is not a
    // convenience: `padding: calc(.5em + 1rem)` and `font-size: 1.25em` in the
    // same rule resolve their `em` against different numbers, and a single pass
    // cannot produce both.
    float parent_font_size = 16.0f;
    if (parent && parent->inherited) {
        for (const declaration & d : parent->inherited->declarations) {
            if (d.property != font_size_) { continue; }
            if (const auto px = css::length_text_to_px(d.value, font_context(16.0f))) {
                parent_font_size = *px;
            }
            break;
        }
    }
    // ...AND THE PARENT'S LINE HEIGHT, for `lh` (CSS Values 4 §6.1.1). The
    // same asymmetry as `em`: `lh` in `font-size` and in `line-height`
    // itself is the parent's, everywhere else the element's own. The
    // parent's line-height is inherited text - a number, a length the
    // parent's cascade already folded to px, a percentage or `normal` - and
    // resolves against the parent's font size.
    //
    // THE ROOT RESOLVES AGAINST THE INITIAL VALUES: `font-size: 1lh` on
    // `:root` is 1.25 times 16px, however the root's own font size and
    // line-height come out (lh-rlh-on-root-001).
    const float parent_line_height =
        parent && parent->inherited
            ? line_height_px(parent->inherited->get(line_height_), parent_font_size)
            : 16.0f * 1.25f;
    // THE ROOT STARTS FROM THE INITIAL VALUES - a `font-size: 3rem` on `:root`
    // is 48px however the previous resolve of this document left the root,
    // and the two members below persist across resolves (rem-unit-root-element).
    if (!parent) {
        root_font_size_ = 16.0f;
        root_line_height_ = parent_line_height;
    }
    // THE PARENT'S `0` ADVANCE, for `ch` in `font-size` and the other font-*
    // properties, where it is the parent's like `em` (CSS Values 4 §6.1.1):
    // the parent's inherited font-family, font-weight and font-style at the
    // parent's size. Measured only when the shell injected a measurement;
    // otherwise zero, and `ch` takes its half-em fallback.
    std::string_view face_family =
        parent && parent->inherited ? parent->inherited->get(font_family_) : std::string_view{};
    std::string_view face_weight =
        parent && parent->inherited ? parent->inherited->get(font_weight_) : std::string_view{};
    std::string_view face_style =
        parent && parent->inherited ? parent->inherited->get(font_style_) : std::string_view{};
    const float parent_zero_advance =
        measure_ ? zero_advance_of(face_family, face_weight, face_style, parent_font_size) : 0.0f;
    float own_font_size = parent_font_size;
    std::vector<atom> cyclic_registered;
    std::vector<atom> read; // what the font-size's substitution looked at
    // Whether the WINNING font-size declaration actually resolved to a length.
    // `font-size: larger` and the other relative keywords are not modelled, and
    // rewriting one to a pixel value would be inventing an answer - so the text
    // survives and whoever reads it decides.
    bool font_size_resolved = false;
    {
        // The em basis for resolving font-size is the PARENT's; the rem basis is
        // the root's, which for the root element is its own answer and so is
        // seeded from the parent's - a root `font-size: 2rem` is circular and CSS
        // resolves it against the initial 16px.
        // A PERCENTAGE IN A FONT SIZE IS OF THE PARENT'S, and the evaluator
        // can be told so: `calc(50% + 1px)` folds here rather than waiting
        // for a containing block it will never be measured against.
        css::length_context ctx =
            font_context(parent_font_size, parent_line_height, parent_zero_advance);
        ctx.percent_basis = parent_font_size;
        conditions.lengths = ctx;
        conditions.property = "font-size";
        // A REGISTERED PROPERTY IN FONT-RELATIVE UNITS DEPENDS ON THIS
        // FONT SIZE, so a font-size reading it is a cycle (CSS Properties
        // and Values API 1 §2.4): the font-size is invalid at computed-value
        // time - inherited - and the property takes its initial value
        // (typed_arithmetic_cycle).
        read.clear();
        conditions.on_read = [&read, this](std::string_view name) {
            read.push_back(atoms_->intern(name));
        };
        fold([&](const declaration & d) {
            if (d.property != font_size_) { return; }
            std::string value{d.value};
            if (css::may_have_var(value)) {
                read.clear();
                const std::optional<std::string> done =
                    css::substitute_var(value, lookup, *atoms_, attributes, &conditions);
                for (const atom name : read) {
                    if (registration_of(name) == nullptr) { continue; }
                    const std::optional<std::string_view> held = lookup(name);
                    if (held && font_relative(*held, !parent)) {
                        cyclic_registered.push_back(name);
                        return;
                    }
                }
                if (!done) { return; }
                value = *done;
            }
            // A calc that does not evaluate keeps its text here, and falls
            // through to the keyword branch below - which is right for a font
            // size, because a keyword is a real answer for one.
            if (css::may_have_math(value)) {
                value = css::fold_math(value, ctx, css::math_context::length).text;
            }
            const std::optional<float> px = css::length_text_to_px(value, ctx);
            // A percentage font size is the parent's, scaled - the one relative
            // form that is not a length and still has an answer here.
            const std::string_view text = trim(value, html_whitespace);
            if (px) {
                own_font_size = *px;
                font_size_resolved = true;
            } else if (text.ends_with('%')) {
                float share = 0;
                const char * begin = text.data();
                if (std::from_chars(begin, begin + text.size() - 1, share).ec == std::errc{}) {
                    own_font_size = parent_font_size * share / 100.0f;
                    font_size_resolved = true;
                }
            } else if (ascii_iequals(text, "inherit")) {
                own_font_size = parent_font_size;
                font_size_resolved = true;
            } else {
                font_size_resolved = false; // a keyword: leave the text alone
            }
        });
        // Only the font-size cares what was read.
        conditions.on_read = nullptr;
    }
    // The root's size is what every `rem` in the document resolves against, so it
    // is recorded as the tree is descended rather than looked up per element.
    if (!parent) { root_font_size_ = own_font_size; }
    // ...AND THE ELEMENT'S OWN `0` ADVANCE, now that its font size is known:
    // its own winning font-family/weight/style declarations over the
    // parent's face (a value still holding a var() is the parent's).
    float own_zero_advance = 0.0f;
    if (measure_) {
        fold([&](const declaration & d) {
            if (d.property != font_family_ && d.property != font_weight_ &&
                d.property != font_style_) {
                return;
            }
            if (css::may_have_var(d.value)) { return; }
            (d.property == font_family_   ? face_family
             : d.property == font_weight_ ? face_weight
                                          : face_style) = d.value;
        });
        own_zero_advance = zero_advance_of(face_family, face_weight, face_style, own_font_size);
        if (!parent) { root_zero_advance_ = own_zero_advance; }
    }
    // PASS ONE AND THREE QUARTERS: LINE HEIGHT, for `lh` in everything else.
    // The winning `line-height` declaration, substituted and folded against
    // the PARENT's `lh` and the element's own `em`, then resolved to px. An
    // absent declaration is the inherited text against the element's OWN
    // font size - a `1.5` is a factor and inherits as one.
    float own_line_height = line_height_px(
        parent && parent->inherited ? parent->inherited->get(line_height_) : "", own_font_size);
    // ...and a percentage in a line-height is of the element's own font size,
    // so `calc(10% / 1px)` is the number 1 here (typed_arithmetic).
    css::length_context line_height_lengths =
        font_context(own_font_size, parent_line_height, own_zero_advance);
    line_height_lengths.percent_basis = own_font_size;
    {
        const css::length_context & ctx = line_height_lengths;
        conditions.lengths = ctx;
        conditions.property = "line-height";
        fold([&](const declaration & d) {
            if (d.property != line_height_) { return; }
            std::string value{d.value};
            if (css::may_have_var(value)) {
                const std::optional<std::string> done =
                    css::substitute_var(value, lookup, *atoms_, attributes, &conditions);
                if (!done) { return; }
                value = *done;
            }
            if (css::may_have_math(value)) { value = css::fold_math(value, ctx).text; }
            // A bare number is a factor, not pixels - which is why
            // length_text_to_px is asked only after from_chars has had its
            // turn; a percentage and `normal` are line_height_px's.
            const std::string_view text = trim(value, html_whitespace);
            float number = 0;
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), number);
            if (ec == std::errc{} && ptr == text.data() + text.size()) {
                own_line_height = number * own_font_size;
            } else if (const auto px = css::length_text_to_px(value, ctx);
                       px && !text.ends_with('%')) {
                own_line_height = *px;
            } else if (ascii_iequals(text, "inherit")) {
                own_line_height = line_height_px(
                    parent && parent->inherited ? parent->inherited->get(line_height_) : "",
                    own_font_size);
            } else {
                own_line_height = line_height_px(value, own_font_size);
            }
        });
    }
    if (!parent) { root_line_height_ = own_line_height; }
    // THE ELEMENT'S OWN WRITING MODE, settled before any length resolves:
    // `vi`/`vb` swap axes in a vertical one (CSS Values 4 §6.1.2), and the
    // property inherits, so the parent's answer stands until a declaration
    // says otherwise - a keyword, never a length, so nothing to fold.
    bool vertical = parent && parent->inherited &&
                    !parent->inherited->get(writing_mode_).starts_with("horizontal") &&
                    !parent->inherited->get(writing_mode_).empty();
    fold([&](const declaration & d) {
        if (d.property != writing_mode_) { return; }
        const std::string_view text = trim(d.value, html_whitespace);
        if (css::may_have_var(text)) { return; }
        // `initial` is horizontal-tb; the other wide keywords keep the
        // parent's answer on an inherited property.
        if (ascii_iequals(text, "initial")) {
            vertical = false;
            return;
        }
        if (css::is_wide_keyword(text)) { return; }
        const std::string lowered = ascii_lower_copy(text);
        vertical = lowered.starts_with("vertical-") || lowered.starts_with("sideways-");
    });
    css::length_context lengths = font_context(own_font_size, own_line_height, own_zero_advance);
    lengths.vertical = vertical;
    // ...EXCEPT IN `line-height` ITSELF, where `lh` is still the parent's -
    // `line-height: 2lh` folded against its own answer would double it -
    // and `line_height_lengths` above is what that property folds with; and
    // EXCEPT IN THE OTHER font-* PROPERTIES, where an `em` is the PARENT's
    // font size as it is in `font-size`: `font-weight: calc(1em / 1px)`
    // under a 10px parent is 10 whatever the element's own size
    // (using-font-relative-units-in-font-properties, CSS Values 4 §6.1.1).
    const css::length_context font_lengths =
        font_context(parent_font_size, parent_line_height, parent_zero_advance);
    const auto lengths_for = [&](atom property) -> const css::length_context & {
        if (property == line_height_) { return line_height_lengths; }
        return atoms_->text(property).starts_with("font-") ? font_lengths : lengths;
    };

    // PASS ONE AND SEVEN EIGHTHS: THE REGISTERED CUSTOM PROPERTIES, CSS
    // Properties and Values API 1 §2.4. An unregistered custom property is
    // a token stream stored verbatim and read lazily; a registered one has
    // a computed value like any other property: its declaration is
    // substituted, parsed against the syntax and computed like the type
    // it names, and what does not parse - or `initial`, or nothing at all
    // on a property that does not inherit - is the initial value. It sits
    // here because a `<length>` computes against the element's own font
    // size, which the passes above have just settled.
    //
    // A property that inherits and is not declared here is left to the
    // parent's inherited half, which already holds its computed value:
    // pushing a copy would give every element an inherited block of its
    // own and undo the sharing the split below depends on.
    for (const auto & [id, registration] : registrations_) {
        const atom name{id};
        declaration * own = nullptr;
        for (declaration & d : out) {
            if (d.property == name) { own = &d; }
        }
        // A `random()` in a registered property is keyed on THAT property
        // (CSS Values 5 §random-caching): `--x` and `--y` on one element
        // draw differently, and `--len-scoped: random(property-scoped, ...)`
        // draws the same on every element (random-computed).
        css::length_context registered_lengths = lengths;
        registered_lengths.property = atoms_->text(name);
        std::optional<std::string> computed;
        const bool cyclic = std::ranges::find(cyclic_registered, name) != cyclic_registered.end();
        if (cyclic) {
            // Part of a cycle through font-size: the initial value.
        } else if (own != nullptr && own->value != guaranteed_invalid) {
            std::string text = own->value;
            bool substituted = true;
            if (css::may_have_var(text)) {
                conditions.lengths = lengths;
                conditions.property = std::string{atoms_->text(name)};
                std::optional<std::string> done =
                    css::substitute_var(text, lookup, *atoms_, attributes, &conditions);
                substituted = done.has_value();
                if (done) { text = std::move(*done); }
            }
            // A SUBSTITUTED CSS-WIDE KEYWORD IS THAT KEYWORD (CSS Values 5
            // §arbitrary-substitution): `attr(data-x type(*))` holding
            // `inherit` is the parent's value, `unset` whichever the
            // registration says (attr-css-wide-keywords).
            const std::string_view word = trim(text, html_whitespace);
            const bool from_parent = ascii_iequals(word, "inherit") ||
                                     (registration.inherits && (ascii_iequals(word, "unset") ||
                                                                ascii_iequals(word, "revert")));
            if (substituted && from_parent) {
                // `get` reads the parent's own half too, where a property
                // that does not inherit lives.
                const std::string_view held = parent ? parent->get(name) : "";
                if (!held.empty()) { computed = std::string{held}; }
            } else if (substituted && !ascii_iequals(word, "initial") &&
                       !ascii_iequals(word, "unset") && !ascii_iequals(word, "revert")) {
                computed = css::compute_registered(text, registration.syntax, registered_lengths);
            }
        } else if (own == nullptr && registration.inherits && parent) {
            continue;
        }
        if (!computed) {
            computed = css::compute_registered(registration.initial, registration.syntax, lengths)
                           .value_or(registration.initial);
        }
        if (own != nullptr) {
            own->value = std::move(*computed);
        } else {
            out.push_back(declaration{name, std::move(*computed)});
        }
    }

    // PASS TWO: everything else. Substitute, then expand, then put.
    fold([&](const declaration & d) {
        const std::string_view property = atoms_->text(d.property);
        if (property.starts_with("--")) { return; }
        std::string value{d.value};
        // `unset`: actively REMOVE the property from what has been folded so
        // far, rather than merely declining to add it. The two are different
        // whenever an earlier declaration set the same property, and which one
        // is right depends on when the value became invalid - see both callers.
        const auto unset = [&] {
            const auto erase = [&out](atom property_to_erase) {
                for (std::size_t i = 0; i < out.size(); ++i) {
                    if (out[i].property == property_to_erase) {
                        out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                        break;
                    }
                }
            };
            // Invalid-at-computed-value time applies to every longhand a
            // shorthand governs. Removing a raw `overflow` declaration
            // would leave an earlier overflow-x/y active, even though the
            // later shorthand won the cascade and became `unset`.
            if (property == "overflow") {
                erase(atoms_->intern("overflow-x"));
                erase(atoms_->intern("overflow-y"));
            } else {
                erase(d.property);
            }
        };
        const bool had_var = css::may_have_var(value);
        // The font-size that was found to be a cycle above is invalid at
        // computed-value time here too.
        if (d.property == font_size_ && !cyclic_registered.empty() && had_var) {
            unset();
            return;
        }
        if (had_var) {
            conditions.lengths = lengths_for(d.property);
            conditions.property = std::string{property};
            const std::optional<std::string> done =
                css::substitute_var(value, lookup, *atoms_, attributes, &conditions);
            // INVALID AT COMPUTED-VALUE TIME means `unset`, which for an inherited
            // property lets the inherited value through and otherwise means absent.
            // NOT "drop it and let an earlier declaration win" - that is the classic
            // wrong reading, and it is observable: `color: red; color: var(--x)`
            // renders as the INHERITED colour in Chrome, not red. So the property is
            // actively removed from what has been folded so far.
            //
            // AN EMPTY RESULT is invalid too, and it is the case Bootstrap hits:
            // it ships seventeen empty-but-valid custom properties, and
            // `body { text-align: var(--bs-body-text-align) }` reads one. An empty
            // token stream is a valid substitution but not a valid VALUE.
            if (!done || trim(*done, html_whitespace).empty()) {
                unset();
                return;
            }
            value = *done;
            // ...AND SO IS A RESULT THE PROPERTY'S GRAMMAR REFUSES. A value
            // is validated when it is parsed, and a substituted one was not
            // parsed until now: `width: attr(data-n type(<number>))` is the
            // number `10`, which `width` cannot take, and CSS Variables 1 §3
            // makes that invalid at computed-value time - `unset`, not
            // ten pixels (attr-all-types). Asked of the property table,
            // which refuses nothing for a property it does not model.
            //
            // ...AND WHAT IT ACCEPTS IT SPELLS, as `el.style` would have:
            // `z-index: var(--n)` with a 25-digit `--n` is the same
            // integer, through the same double, as `el.style.zIndex = n`
            // (serialize-custom-props).
            if (!css::may_have_math(value)) {
                css::value_check checked = css::check_declaration(property, value);
                if (!checked.valid) {
                    unset();
                    return;
                }
                value = std::move(checked.serialized);
            }
        }
        // CALC, AFTER SUBSTITUTION AND BEFORE EXPANSION - the same ordering
        // argument as the shorthands: `-1 * var(x)` has no arithmetic to do
        // before substitution, and `border: calc(var(w) * 2) solid red` cannot
        // be split into longhands until its components are single tokens.
        if (css::may_have_math(value)) {
            // WHAT THE PROPERTY WILL TAKE, passed down, because the evaluator
            // answers with numbers and only the cascade knows whether one is a
            // value here: `opacity: calc(2 / 4)` is `0.5`; `width: calc(2 * 3)`
            // is a syntax error.
            css::length_context math_bases = lengths_for(d.property);
            math_bases.property = property;
            css::folded_value done =
                css::fold_math(value, math_bases, css::math_context_of(property));
            if (!done.ok) {
                // A CALC THAT DOES NOT EVALUATE IS NOT A VALUE, and the
                // declaration is invalid. WHICH KIND of invalid depends on where
                // the value came from, which is why `had_var` is remembered: a
                // value that went through substitution is invalid at
                // COMPUTED-VALUE time, and §3 spells that `unset`, so it must
                // also remove the earlier declaration it beat. One that never
                // contained a var() is invalid at PARSE time, so the earlier
                // declaration simply wins and this one is dropped.
                if (had_var) { unset(); }
                return;
            }
            value = std::move(done.text);
            // ...AND CLAMPED TO THE PROPERTY'S RANGE, CSS Values 4 §10.10:
            // `tab-size: calc(2 * -4)` computes to 0 where a literal `-8`
            // never got past the grammar. `calc-numbers` asks for exactly
            // that, and the table already knows which properties have a
            // floor at zero.
            if (const css::property_syntax * known = css::find_property(property);
                known != nullptr && known->nonnegative) {
                value = css::non_negative(value);
            }
            // ...AND A SUBSTITUTED VALUE IS VALIDATED once its math has an
            // answer, as the path without math was above: `font-style:
            // attr(data-foo type(<angle>), calc(10 + 20))` falls back to
            // the number 30, which font-style cannot take, and is invalid
            // at computed-value time (attr-all-types). A value that is
            // still a function - `min(10px, 5%)` - is valid as it stands.
            if (had_var && !css::may_have_math(value)) {
                const css::value_check checked = css::check_declaration(property, value);
                if (!checked.valid) {
                    unset();
                    return;
                }
                value = std::move(checked.serialized);
            }
        }
        // FONT SIZE IS ALREADY RESOLVED - the pre-pass above did it, because
        // every `em` in every other declaration needed the answer first. Emit
        // that number rather than re-deriving it here, so there is exactly one
        // place a font size is computed and no way for the two to disagree.
        if (d.property == font_size_ && font_size_resolved) {
            value = css::serialize_calc(css::calc_result{own_font_size, 0.0f, false});
        }
        // EVERY RELATIVE LENGTH FOLDS TO PIXELS HERE, after expansion so a
        // shorthand's components each get their own answer. `padding: 1rem 2em`
        // becomes four px longhands, which is what a computed value is.
        //
        // It happens here rather than in layout because this is the only place
        // that knows all three bases at once: the element's own font size, the
        // ROOT's, and the viewport.
        //
        // A BARE NUMBER IS LEFT ALONE: `line-height: 1.5` is not 1.5px.
        //
        // EVERY DIMENSION FAMILY, not only lengths. `transition-delay: 12ms`
        // computes to `0.012s` and `rotate: 100grad` to `90deg` (CSS Values 4
        // 6.4-6.5); `canonical_dimension_text` is a superset of the length case
        // and still answers `96px` for `1in`.
        const auto folded = [&](std::string text) {
            if (auto canonical = css::canonical_dimension_text(text, lengths_for(d.property))) {
                return std::move(*canonical);
            }
            return text;
        };
        const auto expanded = expand_shorthand(property, value);
        if (expanded.empty()) {
            // A substituted token stream is validated only now. If it is
            // not overflow grammar, the winning shorthand is invalid at
            // computed-value time and resets both axes; a parse-time
            // invalid declaration still leaves earlier declarations alone.
            if (had_var && property == "overflow") {
                unset();
                return;
            }
            put(declaration{d.property, folded(std::move(value))});
            return;
        }
        for (const auto & [name, text] : expanded) {
            put(declaration{atoms_->intern_lower(name), folded(std::string{text})});
        }
    });

    // SPLIT THE RESULT. Anything inherited goes into a fresh inherited half built
    // on top of the parent's; everything else stays the element's own.
    //
    // THE LOAD-BEARING SHORTCUT: an element that declared nothing inherited keeps
    // its PARENT'S POINTER verbatim - no copy, no hash, no intern. That is what
    // makes a 128-entry inherited object free for the thousands of elements that
    // share one, and it is the difference between this being an optimisation and
    // being a regression.
    declaration_list inherited_here;
    bool any_inherited = false;
    for (const declaration & d : out) {
        if (inherits(atoms_->text(d.property))) { any_inherited = true; }
    }
    inherited_ptr result_inherited = from;
    if (any_inherited) {
        if (from) { inherited_here = from->declarations; }
        for (const declaration & d : out) {
            if (!inherits(atoms_->text(d.property))) { continue; }
            bool replaced = false;
            for (declaration & existing : inherited_here) {
                if (existing.property == d.property) {
                    existing.value = d.value;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) { inherited_here.push_back(d); }
        }
        result_inherited = table_.intern_inherited(std::move(inherited_here));
    }
    // The own half keeps the inherited properties too. They are redundant with the
    // inherited half - `get` would find them there - but removing them would make
    // an element's own `color: red` invisible to anything that enumerates its own
    // declarations, which is what getComputedStyle's key list is.
    return table_.intern(std::move(out), std::move(result_inherited));
}

void engine::resolve_subtree(const read_txn & txn, node_id node, ancestor_filter & ancestors,
                             style_map & out, std::size_t depth,
                             const computed_style_ptr & parent) {
    if (txn.kind(node).value_or(node_kind::text) != node_kind::element) {
        for (const node_id child : txn.children(node)) {
            resolve_subtree(txn, child, ancestors, out, depth, parent);
        }
        return;
    }
    if (levels_.size() <= depth) { levels_.resize(depth + 1); }
    if (path_.size() <= depth) { path_.resize(depth + 1); }
    if (totals_.size() <= depth) { totals_.resize(depth + 1); }
    // APPENDED BEFORE RESOLVING, so the chain and the sibling list agree about
    // where this element is. Only earlier indices are ever read - a sibling
    // combinator looks backwards only - so being in the list already is safe.
    element_facts my_facts = facts_of(txn, node);
    my_facts.sibling_index = static_cast<std::uint32_t>(levels_[depth].size()) + 1;
    my_facts.sibling_count = totals_[depth].elements;
    my_facts.type_index = totals_[depth].next_for(my_facts.tag);
    my_facts.type_count = totals_[depth].total_for(my_facts.tag);
    root_facts(txn, node, depth, my_facts);
    levels_[depth].push_back(visited_element{node, std::move(my_facts)});
    path_[depth] = levels_[depth].size() - 1;
    const element_facts & self = levels_[depth].back().facts;

    const computed_style_ptr resolved = resolve(txn, node, self, ancestors, depth, parent);
    out[key_of(node)] = resolved;
    if (chain_styles_.size() <= depth) { chain_styles_.resize(depth + 1); }
    chain_styles_[depth] = resolved;

    // The tag, id and classes are read from the stored facts rather than from
    // `self`, because resolve() may have grown levels_ and reallocated it.
    const visited_element & me = levels_[depth][path_[depth]];
    const atom my_tag = me.facts.tag;
    const atom my_id = me.facts.id;
    const boost::container::small_vector<atom, 4> my_classes = me.facts.classes;
    ancestors.push(my_tag, my_id, my_classes);
    // A FRESH SIBLING LIST for this element's children. Cleared once, before the
    // loop: the children accumulate into it as they are visited, which is
    // exactly what `~` needs, and clear() keeps the capacity so a wide document
    // stops allocating after the widest level it has seen.
    enter_level(txn, node, depth + 1);
    for (const node_id child : txn.children(node)) {
        resolve_subtree(txn, child, ancestors, out, depth + 1, resolved);
    }
    ancestors.pop(my_tag, my_id, my_classes);
}

void engine::enter_level(const read_txn & txn, node_id parent, std::size_t depth) {
    if (levels_.size() <= depth) { levels_.resize(depth + 1); }
    if (totals_.size() <= depth) { totals_.resize(depth + 1); }
    levels_[depth].clear();
    level_totals & t = totals_[depth];
    t.elements = 0;
    t.per_tag.clear();
    t.seen_per_tag.clear();
    for (const node_id child : txn.children(parent)) {
        if (txn.kind(child).value_or(node_kind::text) != node_kind::element) { continue; }
        ++t.elements;
        const atom tag = txn.tag(child).value_or(atom{});
        bool found = false;
        for (auto & [seen, n] : t.per_tag) {
            if (seen == tag) {
                ++n;
                found = true;
                break;
            }
        }
        if (!found) { t.per_tag.emplace_back(tag, 1u); }
    }
}

std::string_view engine::language_of(const read_txn & txn, node_id node) const {
    const atom lang = atoms_->intern("lang");
    for (node_id at = node; at; at = txn.parent(at)) {
        if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { break; }
        if (const attribute * a = txn.find_attribute(at, lang)) { return a->value; }
        if (txn.element_ns(at) != node_ns::html) {
            if (const attribute * a =
                    txn.find_attribute_ns(at, "http://www.w3.org/XML/1998/namespace", "lang")) {
                return a->value;
            }
        }
    }
    return pragma_language(txn);
}

std::string_view engine::pragma_language(const read_txn & txn) const {
    const std::uint64_t version = txn.version();
    if (pragma_language_version_ == version) { return pragma_language_; }
    pragma_language_version_ = version;
    pragma_language_.clear();
    const atom head_tag = atoms_->intern("head");
    const atom equiv = atoms_->intern("http-equiv");
    const atom content = atoms_->intern("content");
    // The root IS `<html>` - tree_builder::parse sets it as such - so `<head>`
    // is one of its children, not a grandchild.
    for (const node_id child : txn.children(txn.root())) {
        if (txn.tag(child).value_or(atom{}) != head_tag) { continue; }
        for (const node_id meta : txn.children(child)) {
            if (!ascii_iequals(txn.attribute_value(meta, equiv), "content-language")) { continue; }
            const std::string_view value = txn.attribute_value(meta, content);
            if (value.find(',') != std::string_view::npos) { continue; }
            const std::string_view tag = first_word(value);
            // Each meta sets the pragma as it is processed, so a later one
            // replaces an earlier one - hence no early exit.
            if (!tag.empty()) { pragma_language_ = tag; }
        }
    }
    return pragma_language_;
}

bool engine::language_matches(std::string_view range, std::string_view lang) {
    if (lang.empty() || range.empty()) { return false; }
    std::size_t ri = 0;
    std::size_t li = 0;
    const std::string_view first_range = next_subtag(range, ri);
    const std::string_view first_lang = next_subtag(lang, li);
    if (first_range != "*" && !ascii_iequals(first_range, first_lang)) { return false; }
    while (ri <= range.size()) {
        const std::string_view want = next_subtag(range, ri);
        if (want.empty()) { return true; }
        if (want == "*") { continue; }
        for (;;) {
            if (li > lang.size()) { return false; }
            const std::string_view have = next_subtag(lang, li);
            if (have.empty()) { return false; }
            if (ascii_iequals(want, have)) { break; }
            if (have.size() == 1) { return false; } // a singleton subtag
        }
    }
    return true;
}

bool engine::direction_is_rtl(const read_txn & txn, node_id node) const {
    const atom dir = atoms_->intern("dir");
    for (node_id at = node; at; at = txn.parent(at)) {
        if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { break; }
        const std::string_view value = txn.attribute_value(at, dir);
        if (ascii_iequals(value, "rtl")) { return true; }
        if (ascii_iequals(value, "ltr")) { return false; }
        const bool bdi = txn.tag(at).value_or(atom{}) == atoms_->intern("bdi") &&
                         txn.element_ns(at) == node_ns::html;
        if (ascii_iequals(value, "auto") || bdi) {
            // Nothing strong anywhere in the subtree leaves `rtl` false, which
            // is the spec's answer too: `dir=auto` over digits alone is ltr.
            bool rtl = false;
            (void)first_strong(txn, at, rtl);
            return rtl;
        }
    }
    return false;
}

bool engine::first_strong(const read_txn & txn, node_id node, bool & rtl) const {
    const node_kind kind = txn.kind(node).value_or(node_kind::comment);
    if (is_text_kind(kind)) { return first_strong_in(txn.text(node), rtl); }
    if (kind != node_kind::element) { return false; }
    const atom dir_key = atoms_->intern("dir");
    for (const node_id child : txn.children(node)) {
        // HTML §3.2.6.4: a descendant with a `dir` of its own, a <bdi>, a
        // <script>, <style> or <textarea> is not part of the text the auto
        // direction is read from (dir-selector-auto's `div3`).
        if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
            const std::string_view dir = txn.attribute_value(child, dir_key);
            if (ascii_iequals(dir, "ltr") || ascii_iequals(dir, "rtl") ||
                ascii_iequals(dir, "auto")) {
                continue;
            }
            const std::string_view tag = atoms_->text(txn.tag(child).value_or(atom{}));
            if (tag == "bdi" || tag == "script" || tag == "style" || tag == "textarea") {
                continue;
            }
        }
        if (first_strong(txn, child, rtl)) { return true; }
    }
    return false;
}

bool engine::first_strong_in(std::string_view text, bool & rtl) {
    for (std::size_t i = 0; i < text.size();) {
        const char32_t cp = decode_utf8(text, i);
        // Hebrew, Arabic, Syriac, Thaana, NKo, Samaritan and Mandaic; then the
        // Arabic Extended, presentation and supplement blocks; then the RTL
        // planes - Cypriot through Adlam - in the SMP.
        const bool is_rtl = (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) ||
                            (cp >= 0xFE70 && cp <= 0xFEFF) || (cp >= 0x10800 && cp <= 0x10FFF) ||
                            (cp >= 0x1E800 && cp <= 0x1EFFF);
        if (is_rtl) {
            rtl = true;
            return true;
        }
        // Strong left-to-right is every letter that is not one of the above.
        // Digits, punctuation and whitespace are neutral and keep the scan
        // going, which is the entire point of `dir=auto`.
        const bool is_ltr = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp >= 0x00C0;
        if (is_ltr) {
            rtl = false;
            return true;
        }
    }
    return false;
}

std::string_view engine::first_word(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t\n\f\r");
    if (begin == std::string_view::npos) { return {}; }
    const std::size_t end = text.find_first_of(" \t\n\f\r", begin);
    return text.substr(begin, end == std::string_view::npos ? end : end - begin);
}

std::string_view engine::next_subtag(std::string_view tag, std::size_t & at) {
    if (at > tag.size()) { return {}; }
    const std::size_t dash = tag.find('-', at);
    const std::size_t end = dash == std::string_view::npos ? tag.size() : dash;
    const std::string_view out = tag.substr(at, end - at);
    at = end + 1;
    return out;
}

bool engine::matches_from(const read_txn & txn, const ancestor_filter & ancestors,
                          const compiled_selector & sel, std::size_t at_depth,
                          std::size_t at_index) const {
    if (!compound_matches(txn, ancestors, sel.parts.front(), at_depth, at_index)) { return false; }
    for (std::size_t i = 1; i < sel.parts.size(); ++i) {
        const compound & want = sel.parts[i];
        switch (sel.links[i - 1]) {
        case combinator::child: {
            if (at_depth == 0) { return false; }
            --at_depth;
            at_index = path_[at_depth];
            if (!compound_matches(txn, ancestors, want, at_depth, at_index)) { return false; }
            break;
        }
        case combinator::descendant: {
            // The filter's whole job: reject a descendant selector before walking
            // a single ancestor. It has no false negatives, so a `false` here is
            // conclusive.
            //
            // CONSULTED FOR DESCENDANT ONLY, and that is not an oversight. The
            // filter holds the SUBJECT's ancestors; a sibling is not one of them,
            // so asking it about a sibling combinator would be a false NEGATIVE -
            // it would reject a selector that does match, and the page would
            // render wrong. The saturating counters exist to prevent exactly that
            // class of error from the other direction.
            if (!ancestors.may_match(want)) { return false; }
            bool found = false;
            for (std::size_t up = at_depth; up-- > 0;) {
                if (compound_matches(txn, ancestors, want, up, path_[up])) {
                    at_depth = up;
                    at_index = path_[up];
                    found = true;
                    break;
                }
            }
            if (!found) { return false; }
            break;
        }
        case combinator::next_sibling: {
            if (at_index == 0) { return false; } // nothing precedes it
            --at_index;
            if (!compound_matches(txn, ancestors, want, at_depth, at_index)) { return false; }
            break;
        }
        case combinator::subsequent_sibling: {
            bool found = false;
            for (std::size_t k = at_index; k-- > 0;) {
                if (compound_matches(txn, ancestors, want, at_depth, k)) {
                    at_index = k;
                    found = true;
                    break;
                }
            }
            if (!found) { return false; }
            break;
        }
        case combinator::none: return false; // only ever the rightmost compound
        }
    }
    return true;
}

float engine::zero_advance_of(std::string_view family_list, std::string_view weight,
                              std::string_view style_text, float font_size) {
    if (!measure_) { return 0.0f; }
    std::string_view family = trim(family_list, html_whitespace);
    if (const std::size_t comma = family.find(','); comma != std::string_view::npos) {
        family = trim(family.substr(0, comma), html_whitespace);
    }
    family = unquoted(family);
    int numeric = 0;
    const auto parsed = std::from_chars(weight.data(), weight.data() + weight.size(), numeric);
    const bool bold = parsed.ec == std::errc{}
                          ? numeric >= 600
                          : ascii_iequals(weight, "bold") || ascii_iequals(weight, "bolder");
    const bool italic =
        ascii_iequals(style_text, "italic") || ascii_istarts_with(style_text, "oblique");
    std::string key{family};
    key += bold ? "|b|" : "|r|";
    key += italic ? "i|" : "u|";
    key += std::to_string(font_size);
    const auto found = zero_advances_.find(key);
    if (found != zero_advances_.end()) { return found->second; }
    const float advance = measure_("0", font_size, family, bold, italic);
    zero_advances_.emplace(std::move(key), advance);
    return advance;
}

} // namespace ctbrowser::style
