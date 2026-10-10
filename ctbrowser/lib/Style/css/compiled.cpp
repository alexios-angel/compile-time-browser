//===- ctbrowser/lib/Style/css/compiled.cpp - Compiled stylesheets --------===//
//
// compile() flattens a parsed stylesheet into compiled_sheet_data, and
// materialize() rebuilds the stylesheet from a compiled_sheet. See
// ctbrowser/style/css/compiled.hpp.
//
// This file must not call the tokenizer or the parser: a program that loads
// compiled sheets links it so that it does not have to link them.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/style/css/compiled.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

namespace ctbrowser::style::css {

namespace {

/// The chunks of a text_chunks joined into one string.
[[nodiscard]] std::string joined(text_chunks chunks) {
    std::size_t size = 0;
    for (const std::string_view chunk : chunks) { size += chunk.size(); }
    std::string out;
    out.reserve(size);
    for (const std::string_view chunk : chunks) { out += chunk; }
    return out;
}

template <class T> [[nodiscard]] std::uint32_t size32(const std::vector<T> & v) {
    return static_cast<std::uint32_t>(v.size());
}

/// Builds compiled_sheet_data from one stylesheet.
class flattener {
public:
    flattener(const stylesheet & sheet, const atom_table & atoms) : sheet_(sheet), atoms_(atoms) {}

    compiled_sheet_data run() {
        out_.pool = sheet_.pool;
        out_.prefixes_checked = sheet_.prefixes_checked;
        out_.forgiving = sheet_.forgiving;
        out_.source_length = sheet_.source_length;
        for (const namespace_declaration & n : sheet_.namespaces) {
            out_.namespaces.push_back({text(n.prefix), text(n.uri)});
        }
        for (const import_statement & i : sheet_.imports) {
            out_.imports.push_back({text(i.href), text(i.media), text(i.layer), i.layered,
                                    text(i.supports), text(i.scope), i.scoped, i.begin, i.end});
        }
        out_.tokens = sheet_.tokens;
        out_.values = sheet_.values;
        // The sheet's own selectors take the first slots, so a rule's
        // selector index means the same in both forms; nested ones follow.
        out_.selectors.resize(sheet_.selectors.size());
        out_.selector_count = size32(out_.selectors);
        for (std::size_t i = 0; i < sheet_.selectors.size(); ++i) {
            const compiled_complex filled = complex(sheet_.selectors[i]);
            out_.selectors[i] = filled;
        }
        for (const raw_declaration & d : sheet_.declarations) {
            out_.declarations.push_back({atom_text(d.property), d.first_value, d.value_count,
                                         d.text, d.length, d.order, d.important, d.custom});
        }
        out_.rules = sheet_.rules;
        out_.font_faces = sheet_.font_faces;
        out_.properties = sheet_.properties;
        out_.functions = sheet_.functions;
        for (const keyframes_block & k : sheet_.keyframes) {
            const compiled_range frames{size32(out_.keyframe_blocks),
                                        static_cast<std::uint32_t>(k.frames.size())};
            for (const keyframe_block & frame : k.frames) {
                const compiled_range offsets{size32(out_.offsets),
                                             static_cast<std::uint32_t>(frame.offsets.size())};
                out_.offsets.insert(out_.offsets.end(), frame.offsets.begin(), frame.offsets.end());
                out_.keyframe_blocks.push_back(
                    {offsets, frame.first_declaration, frame.declaration_count});
            }
            out_.keyframes.push_back({text(k.name), k.condition, k.layer, frames});
        }
        for (const media_condition & c : sheet_.conditions) {
            const compiled_range queries{size32(out_.media_queries),
                                         static_cast<std::uint32_t>(c.queries.size())};
            for (const media_query & q : c.queries) {
                out_.media_queries.push_back({q.type, q.negated, q.malformed, text(q.condition)});
            }
            out_.conditions.push_back({c.parent, queries});
        }
        for (const std::string & layer : sheet_.layers) { out_.layers.push_back(text(layer)); }
        out_.scopes = sheet_.scopes;
        for (const container_condition & c : sheet_.containers) {
            const compiled_range queries{size32(out_.container_queries),
                                         static_cast<std::uint32_t>(c.queries.size())};
            for (const container_query & q : c.queries) {
                out_.container_queries.push_back({text(q.name), text(q.condition)});
            }
            out_.containers.push_back({c.parent, queries});
        }
        return std::move(out_);
    }

private:
    /// `s` as a slice of the strings block, shared with every earlier use of
    /// the same text: Bootstrap repeats each class name in many selectors.
    compiled_text text(std::string_view s) {
        if (s.empty()) { return {}; }
        if (const auto found = seen_.find(std::string{s}); found != seen_.end()) {
            return found->second;
        }
        const compiled_text made{static_cast<std::uint32_t>(out_.strings.size()),
                                 static_cast<std::uint32_t>(s.size())};
        out_.strings += s;
        seen_.emplace(std::string{s}, made);
        return made;
    }
    compiled_text atom_text(atom a) { return text(atoms_.text(a)); }

    compiled_complex complex(const compiled_selector & selector) {
        compiled_complex made;
        made.specificity = selector.spec.packed;
        made.explicit_scope = selector.explicit_scope;
        made.links = {size32(out_.combinators), static_cast<std::uint32_t>(selector.links.size())};
        out_.combinators.insert(out_.combinators.end(), selector.links.begin(),
                                selector.links.end());
        // The compounds are a contiguous run, so their slots are taken before
        // any of them is filled: filling one can append nested selectors'
        // compounds, which must land after the run.
        made.parts = {size32(out_.compounds), static_cast<std::uint32_t>(selector.parts.size())};
        out_.compounds.resize(out_.compounds.size() + selector.parts.size());
        for (std::size_t i = 0; i < selector.parts.size(); ++i) {
            const compiled_compound filled = compound_of(selector.parts[i]);
            out_.compounds[made.parts.first + i] = filled;
        }
        return made;
    }

    compiled_compound compound_of(const compound & c) {
        compiled_compound made;
        made.tag = atom_text(c.tag);
        made.tag_exact = atom_text(c.tag_exact);
        made.id = atom_text(c.id);
        made.classes = {size32(out_.texts), static_cast<std::uint32_t>(c.classes.size())};
        for (const atom name : c.classes) { out_.texts.push_back(atom_text(name)); }
        made.attributes = {size32(out_.attributes),
                           static_cast<std::uint32_t>(c.attributes.size())};
        for (const attribute_match & a : c.attributes) {
            out_.attributes.push_back({atom_text(a.name), atom_text(a.name_exact), text(a.value),
                                       a.op, a.case_insensitive, a.case_sensitive_flag, a.ns,
                                       atom_text(a.ns_uri)});
        }
        made.states = c.states;
        made.structural = c.structural;
        made.states_after_pe = c.states_after_pe;
        made.structural_after_pe = c.structural_after_pe;
        // As for compounds: the pseudo run is reserved before its entries are
        // filled, because an argument list appends more pseudos.
        made.pseudos = {size32(out_.pseudos), static_cast<std::uint32_t>(c.pseudos.size())};
        out_.pseudos.resize(out_.pseudos.size() + c.pseudos.size());
        for (std::size_t i = 0; i < c.pseudos.size(); ++i) {
            const compiled_pseudo filled = pseudo_of(c.pseudos[i]);
            out_.pseudos[made.pseudos.first + i] = filled;
        }
        made.ns = c.ns;
        made.ns_name = atom_text(c.ns_name);
        made.ns_uri = atom_text(c.ns_uri);
        made.pseudo_element = atom_text(c.pseudo_element);
        made.nesting = c.nesting;
        made.never_matches = c.never_matches;
        made.dropped = c.dropped;
        return made;
    }

    compiled_pseudo pseudo_of(const pseudo_ref & p) {
        compiled_pseudo made;
        made.kind = p.kind;
        made.a = p.a;
        made.b = p.b;
        made.relative = p.relative;
        made.nesting = p.nesting;
        made.args = {size32(out_.selectors), static_cast<std::uint32_t>(p.args.size())};
        out_.selectors.resize(out_.selectors.size() + p.args.size());
        for (std::size_t i = 0; i < p.args.size(); ++i) {
            const compiled_complex filled = complex(p.args[i]);
            out_.selectors[made.args.first + i] = filled;
        }
        made.ranges = {size32(out_.texts), static_cast<std::uint32_t>(p.ranges.size())};
        for (const std::string & range : p.ranges) { out_.texts.push_back(text(range)); }
        made.levels = {size32(out_.levels), static_cast<std::uint32_t>(p.levels.size())};
        out_.levels.insert(out_.levels.end(), p.levels.begin(), p.levels.end());
        return made;
    }

    const stylesheet & sheet_;
    const atom_table & atoms_;
    compiled_sheet_data out_;
    std::unordered_map<std::string, compiled_text> seen_;
};

/// Rebuilds a stylesheet from a compiled_sheet.
class rebuilder {
public:
    rebuilder(const compiled_sheet & sheet, atom_table & atoms)
        : sheet_(sheet), atoms_(atoms), strings_(joined(sheet.strings)) {}

    stylesheet run() const {
        stylesheet out;
        out.pool = joined(sheet_.pool);
        out.prefixes_checked = sheet_.prefixes_checked;
        out.forgiving = sheet_.forgiving;
        out.source_length = sheet_.source_length;
        for (const compiled_namespace & n : sheet_.namespaces) {
            out.namespaces.push_back({string(n.prefix), string(n.uri)});
        }
        for (const compiled_import & i : sheet_.imports) {
            import_statement made;
            made.href = string(i.href);
            made.media = string(i.media);
            made.layer = string(i.layer);
            made.layered = i.layered;
            made.supports = string(i.supports);
            made.scope = string(i.scope);
            made.scoped = i.scoped;
            made.begin = static_cast<std::size_t>(i.begin);
            made.end = static_cast<std::size_t>(i.end);
            out.imports.push_back(std::move(made));
        }
        out.tokens.assign(sheet_.tokens.begin(), sheet_.tokens.end());
        out.values.assign(sheet_.values.begin(), sheet_.values.end());
        out.selectors.reserve(sheet_.selector_count);
        for (std::uint32_t i = 0; i < sheet_.selector_count; ++i) {
            out.selectors.push_back(complex(sheet_.selectors[i]));
        }
        for (const compiled_declaration & d : sheet_.declarations) {
            raw_declaration made;
            made.property = intern(d.property);
            made.first_value = d.first_value;
            made.value_count = d.value_count;
            made.text = d.text;
            made.length = d.length;
            made.order = d.order;
            made.important = d.important;
            made.custom = d.custom;
            out.declarations.push_back(made);
        }
        out.rules.assign(sheet_.rules.begin(), sheet_.rules.end());
        out.font_faces.assign(sheet_.font_faces.begin(), sheet_.font_faces.end());
        out.properties.assign(sheet_.properties.begin(), sheet_.properties.end());
        out.functions.assign(sheet_.functions.begin(), sheet_.functions.end());
        for (const compiled_keyframes & k : sheet_.keyframes) {
            keyframes_block made;
            made.name = string(k.name);
            made.condition = k.condition;
            made.layer = k.layer;
            for (const compiled_keyframe & frame : slice(sheet_.keyframe_blocks, k.frames)) {
                const auto offsets = slice(sheet_.offsets, frame.offsets);
                made.frames.push_back({std::vector<double>(offsets.begin(), offsets.end()),
                                       frame.first_declaration, frame.declaration_count});
            }
            out.keyframes.push_back(std::move(made));
        }
        out.conditions.clear();
        for (const compiled_media_condition & c : sheet_.conditions) {
            media_condition made;
            made.parent = c.parent;
            for (const compiled_media_query & q : slice(sheet_.media_queries, c.queries)) {
                made.queries.push_back({q.type, q.negated, q.malformed, string(q.condition)});
            }
            out.conditions.push_back(std::move(made));
        }
        for (const compiled_text & layer : sheet_.layers) { out.layers.push_back(string(layer)); }
        out.scopes.assign(sheet_.scopes.begin(), sheet_.scopes.end());
        for (const compiled_container_condition & c : sheet_.containers) {
            container_condition made;
            made.parent = c.parent;
            for (const compiled_container_query & q : slice(sheet_.container_queries, c.queries)) {
                made.queries.push_back({string(q.name), string(q.condition)});
            }
            out.containers.push_back(std::move(made));
        }
        return out;
    }

private:
    template <class T>
    [[nodiscard]] static std::span<const T> slice(std::span<const T> table, compiled_range r) {
        return table.subspan(r.first, r.count);
    }
    [[nodiscard]] std::string_view view(compiled_text t) const {
        return std::string_view{strings_}.substr(t.offset, t.length);
    }
    [[nodiscard]] std::string string(compiled_text t) const { return std::string{view(t)}; }
    [[nodiscard]] atom intern(compiled_text t) const { return atoms_.intern(view(t)); }

    [[nodiscard]] compiled_selector complex(const compiled_complex & c) const {
        compiled_selector made;
        made.spec = specificity{c.specificity};
        made.explicit_scope = c.explicit_scope;
        for (const combinator link : slice(sheet_.combinators, c.links)) {
            made.links.push_back(link);
        }
        for (const compiled_compound & part : slice(sheet_.compounds, c.parts)) {
            made.parts.push_back(compound_of(part));
        }
        return made;
    }

    [[nodiscard]] compound compound_of(const compiled_compound & c) const {
        compound made;
        made.tag = intern(c.tag);
        made.tag_exact = intern(c.tag_exact);
        made.id = intern(c.id);
        for (const compiled_text & name : slice(sheet_.texts, c.classes)) {
            made.classes.push_back(intern(name));
        }
        for (const compiled_attribute & a : slice(sheet_.attributes, c.attributes)) {
            attribute_match match;
            match.name = intern(a.name);
            match.name_exact = intern(a.name_exact);
            match.value = string(a.value);
            match.op = a.op;
            match.case_insensitive = a.case_insensitive;
            match.case_sensitive_flag = a.case_sensitive_flag;
            match.ns = a.ns;
            match.ns_uri = intern(a.ns_uri);
            made.attributes.push_back(std::move(match));
        }
        made.states = c.states;
        made.structural = c.structural;
        made.states_after_pe = c.states_after_pe;
        made.structural_after_pe = c.structural_after_pe;
        for (const compiled_pseudo & p : slice(sheet_.pseudos, c.pseudos)) {
            made.pseudos.push_back(pseudo_of(p));
        }
        made.ns = c.ns;
        made.ns_name = intern(c.ns_name);
        made.ns_uri = intern(c.ns_uri);
        made.pseudo_element = intern(c.pseudo_element);
        made.nesting = c.nesting;
        made.never_matches = c.never_matches;
        made.dropped = c.dropped;
        return made;
    }

    [[nodiscard]] pseudo_ref pseudo_of(const compiled_pseudo & p) const {
        pseudo_ref made;
        made.kind = p.kind;
        made.a = p.a;
        made.b = p.b;
        for (const compiled_complex & arg : slice(sheet_.selectors, p.args)) {
            made.args.push_back(complex(arg));
        }
        made.relative = p.relative;
        made.nesting = p.nesting;
        for (const compiled_text & range : slice(sheet_.texts, p.ranges)) {
            made.ranges.push_back(string(range));
        }
        const auto levels = slice(sheet_.levels, p.levels);
        made.levels.assign(levels.begin(), levels.end());
        return made;
    }

    const compiled_sheet & sheet_;
    atom_table & atoms_;
    std::string strings_; // sheet_.strings joined: every compiled_text indexes it.
};

} // namespace

compiled_sheet compiled_sheet_data::view() noexcept {
    pool_chunk_ = pool;
    strings_chunk_ = strings;
    compiled_sheet out;
    out.pool = text_chunks{&pool_chunk_, 1};
    out.strings = text_chunks{&strings_chunk_, 1};
    out.prefixes_checked = prefixes_checked;
    out.forgiving = forgiving;
    out.source_length = source_length;
    out.namespaces = namespaces;
    out.imports = imports;
    out.tokens = tokens;
    out.values = values;
    out.selectors = selectors;
    out.selector_count = selector_count;
    out.compounds = compounds;
    out.combinators = combinators;
    out.attributes = attributes;
    out.pseudos = pseudos;
    out.texts = texts;
    out.levels = levels;
    out.declarations = declarations;
    out.rules = rules;
    out.font_faces = font_faces;
    out.properties = properties;
    out.functions = functions;
    out.keyframes = keyframes;
    out.keyframe_blocks = keyframe_blocks;
    out.offsets = offsets;
    out.conditions = conditions;
    out.media_queries = media_queries;
    out.layers = layers;
    out.scopes = scopes;
    out.containers = containers;
    out.container_queries = container_queries;
    return out;
}

compiled_sheet_data compile(const stylesheet & sheet, const atom_table & atoms) {
    return flattener{sheet, atoms}.run();
}

stylesheet materialize(const compiled_sheet & sheet, atom_table & atoms) {
    return rebuilder{sheet, atoms}.run();
}

token_stream materialize(const compiled_tokens & tokens) {
    token_stream out;
    out.pool = joined(tokens.pool);
    out.tokens.assign(tokens.tokens.begin(), tokens.tokens.end());
    out.source_length = tokens.source_length;
    return out;
}

} // namespace ctbrowser::style::css
