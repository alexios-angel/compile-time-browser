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

} // namespace ctbrowser::style
