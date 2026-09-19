#include "internal.hpp"

namespace ctbrowser::html {

using namespace treebuilder_detail;

namespace {

struct adjustment {
    std::string_view lower;
    std::string_view adjusted;
};

// 13.2.6.5 "adjust SVG attributes": the table as the specification lists it.
constexpr adjustment svg_attributes[] = {
    {"attributename", "attributeName"},
    {"attributetype", "attributeType"},
    {"basefrequency", "baseFrequency"},
    {"baseprofile", "baseProfile"},
    {"calcmode", "calcMode"},
    {"clippathunits", "clipPathUnits"},
    {"diffuseconstant", "diffuseConstant"},
    {"edgemode", "edgeMode"},
    {"filterunits", "filterUnits"},
    {"glyphref", "glyphRef"},
    {"gradienttransform", "gradientTransform"},
    {"gradientunits", "gradientUnits"},
    {"kernelmatrix", "kernelMatrix"},
    {"kernelunitlength", "kernelUnitLength"},
    {"keypoints", "keyPoints"},
    {"keysplines", "keySplines"},
    {"keytimes", "keyTimes"},
    {"lengthadjust", "lengthAdjust"},
    {"limitingconeangle", "limitingConeAngle"},
    {"markerheight", "markerHeight"},
    {"markerunits", "markerUnits"},
    {"markerwidth", "markerWidth"},
    {"maskcontentunits", "maskContentUnits"},
    {"maskunits", "maskUnits"},
    {"numoctaves", "numOctaves"},
    {"pathlength", "pathLength"},
    {"patterncontentunits", "patternContentUnits"},
    {"patterntransform", "patternTransform"},
    {"patternunits", "patternUnits"},
    {"pointsatx", "pointsAtX"},
    {"pointsaty", "pointsAtY"},
    {"pointsatz", "pointsAtZ"},
    {"preservealpha", "preserveAlpha"},
    {"preserveaspectratio", "preserveAspectRatio"},
    {"primitiveunits", "primitiveUnits"},
    {"refx", "refX"},
    {"refy", "refY"},
    {"repeatcount", "repeatCount"},
    {"repeatdur", "repeatDur"},
    {"requiredextensions", "requiredExtensions"},
    {"requiredfeatures", "requiredFeatures"},
    {"specularconstant", "specularConstant"},
    {"specularexponent", "specularExponent"},
    {"spreadmethod", "spreadMethod"},
    {"startoffset", "startOffset"},
    {"stddeviation", "stdDeviation"},
    {"stitchtiles", "stitchTiles"},
    {"surfacescale", "surfaceScale"},
    {"systemlanguage", "systemLanguage"},
    {"tablevalues", "tableValues"},
    {"targetx", "targetX"},
    {"targety", "targetY"},
    {"textlength", "textLength"},
    {"viewbox", "viewBox"},
    {"viewtarget", "viewTarget"},
    {"xchannelselector", "xChannelSelector"},
    {"ychannelselector", "yChannelSelector"},
    {"zoomandpan", "zoomAndPan"},
};

// 13.2.6.5, the SVG start tag's tag-name table.
constexpr adjustment svg_tags[] = {
    {"altglyph", "altGlyph"},
    {"altglyphdef", "altGlyphDef"},
    {"altglyphitem", "altGlyphItem"},
    {"animatecolor", "animateColor"},
    {"animatemotion", "animateMotion"},
    {"animatetransform", "animateTransform"},
    {"clippath", "clipPath"},
    {"feblend", "feBlend"},
    {"fecolormatrix", "feColorMatrix"},
    {"fecomponenttransfer", "feComponentTransfer"},
    {"fecomposite", "feComposite"},
    {"feconvolvematrix", "feConvolveMatrix"},
    {"fediffuselighting", "feDiffuseLighting"},
    {"fedisplacementmap", "feDisplacementMap"},
    {"fedistantlight", "feDistantLight"},
    {"fedropshadow", "feDropShadow"},
    {"feflood", "feFlood"},
    {"fefunca", "feFuncA"},
    {"fefuncb", "feFuncB"},
    {"fefuncg", "feFuncG"},
    {"fefuncr", "feFuncR"},
    {"fegaussianblur", "feGaussianBlur"},
    {"feimage", "feImage"},
    {"femerge", "feMerge"},
    {"femergenode", "feMergeNode"},
    {"femorphology", "feMorphology"},
    {"feoffset", "feOffset"},
    {"fepointlight", "fePointLight"},
    {"fespecularlighting", "feSpecularLighting"},
    {"fespotlight", "feSpotLight"},
    {"fetile", "feTile"},
    {"feturbulence", "feTurbulence"},
    {"foreignobject", "foreignObject"},
    {"glyphref", "glyphRef"},
    {"lineargradient", "linearGradient"},
    {"radialgradient", "radialGradient"},
    {"textpath", "textPath"},
};

[[nodiscard]] std::string_view adjust(std::span<const adjustment> table, std::string_view lower) {
    for (const adjustment & a : table) {
        if (a.lower == lower) { return a.adjusted; }
    }
    return lower;
}

} // namespace

std::string_view adjust_svg_attribute(std::string_view lower) {
    return adjust(svg_attributes, lower);
}

std::string_view adjust_svg_tag(std::string_view lower) {
    return adjust(svg_tags, lower);
}

// ============================================================================
// FOREIGN CONTENT, 13.2.6.5
// ============================================================================

void tree_builder::process_foreign(const token & t) {
    switch (t.kind) {
    case token_kind::character: {
        // NUL is U+FFFD here - by its own rule, which leaves frameset-ok
        // alone; any other non-whitespace character clears it.
        std::string data;
        bool other = false;
        for (const char c : t.data) {
            if (c == '\0') {
                data += "\xEF\xBF\xBD";
            } else {
                data += c;
                if (!is_whitespace(c)) { other = true; }
            }
        }
        insert_text(data);
        if (other) { frameset_ok_ = false; }
        return;
    }
    case token_kind::comment:
    case token_kind::processing_instruction: return insert_comment(t);
    case token_kind::doctype: return;
    case token_kind::start_tag: {
        const std::string & tag = t.name;
        // The breakout tags, and <font> with a color/face/size attribute:
        // pop the foreign elements and handle the tag as HTML.
        bool breakout = breaks_out_of_foreign_content(tag);
        if (!breakout && tag == "font") {
            breakout = std::ranges::any_of(t.attributes, [](const token_attribute & a) {
                return a.name == "color" || a.name == "face" || a.name == "size";
            });
        }
        if (breakout) {
            pop_to_html_context(t.source_begin);
            return process(t, mode_);
        }
        // An ordinary foreign element, in the namespace of the adjusted
        // current node - which is the context element for a fragment parsed
        // on an SVG or MathML element - with its names adjusted in
        // create_element.
        const node_ns ns = adjusted_current()->ns;
        const node_id element = insert_element(t, ns);
        if (ns == node_ns::svg && tag == "svg") { open_foreign(t); }
        if (t.self_closing) {
            if (ns == node_ns::svg && tag == "svg") { close_foreign(t.source_end); }
            pop();
            // `<script/>` in SVG runs, as the end tag would (13.2.6.5).
            if (ns == node_ns::svg && tag == "script") { run_script(element); }
        }
        return;
    }
    case token_kind::end_tag: {
        const std::string & tag = t.name;
        // `</br>` and `</p>`: out of the foreign elements, then the HTML
        // rules - which make a <br> or a <p> beside the graphic.
        if (tag == "br" || tag == "p") {
            pop_to_html_context(t.source_begin);
            return process(t, mode_);
        }
        // An SVG <script> end tag runs the script (13.2.6.5 says so
        // explicitly); everything else walks the stack for a matching
        // foreign element, case-insensitively, and pops to it - or hands the
        // tag to the HTML rules at the first HTML element. The root of a
        // fragment parse is never popped (the "node is the topmost" step).
        for (std::size_t i = open_.size(); i-- > 0;) {
            const entry & node = open_[i];
            if (i == 0 && !context_.empty()) { return; }
            if (node.ns == node_ns::html) { return process(t, mode_); }
            if (ascii_iequals(node.tag, tag)) {
                const node_id closed = node.id;
                if (node.ns == node_ns::svg && node.tag == "svg") { close_foreign(t.source_end); }
                while (open_.size() > i) { pop(); }
                if (node.ns == node_ns::svg && tag == "script") { run_script(closed); }
                return;
            }
        }
        return;
    }
    default: return;
    }
}

void tree_builder::pop_to_html_context(std::size_t source_end) {
    while (!open_.empty()) {
        const entry & top = open_.back();
        if (top.ns == node_ns::html || top.html_integration_point ||
            (is_mathml(top.ns) && is_mathml_text_integration_point(top.tag))) {
            return;
        }
        if (top.ns == node_ns::svg && top.tag == "svg") { close_foreign(source_end); }
        pop();
    }
}

void tree_builder::open_foreign(const token & t) {
    // Only the OUTERMOST <svg> is captured; an inner one is part of the same
    // graphic and travels with it. The depth counter is what distinguishes
    // them, and it is why a nested <svg> does not truncate the capture at the
    // first close tag.
    if (foreign_depth_++ == 0 && !open_.empty()) {
        foreign_node_ = open_.back().id;
        foreign_begin_ = t.source_begin;
    }
}

void tree_builder::close_foreign(std::size_t source_end) {
    if (foreign_depth_ == 0) { return; }
    if (--foreign_depth_ > 0) { return; }
    if (!foreign_node_) { return; }
    if (source_end > foreign_begin_ && source_end <= input_.size()) {
        std::string source{input_.substr(foreign_begin_, source_end - foreign_begin_)};
        // TERMINATE IT IF THE DOCUMENT DID NOT. This span can end at a breakout
        // tag or at EOF rather than at a </svg>, and plutosvg parses strictly:
        // measured, an unclosed <svg> renders NOTHING AT ALL, not a partial
        // graphic. One synthetic close tag is the difference between a page
        // that forgot </svg> drawing what it has and drawing a blank box.
        if (!source.ends_with("</svg>")) { source += "</svg>"; }
        foreign_sources_.emplace_back(foreign_node_, std::move(source));
    }
    foreign_node_ = node_id{};
}

} // namespace ctbrowser::html
