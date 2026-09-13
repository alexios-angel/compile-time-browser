// dom_bindings - reflection: the table of reflected IDL attributes, HTML 2.6,
// and the one getter and one setter every row is answered by.

#include "internal.hpp"

#include <charconv>

namespace ctbrowser::shell {

using namespace detail;

// --- REFLECTION, AND THE INTERFACE OBJECTS THE ACCESSORS LIVE ON ------------
//
// "Reflecting content attributes in IDL attributes" is HTML section 2.6, and it
// is one paragraph per TYPE and a table per element - which is exactly the shape
// it has here. Before this there were twelve names (`id`, `className`, `href`,
// `download`, `target`, `rel`, `alt`, `title`, `name`, `placeholder`, `type`,
// `htmlFor`) installed as string accessors on EVERY wrapper, so `div.href`
// existed, `input.maxLength` did not, `details.open` was a string rather than a
// boolean, and `td.colSpan` was undefined. `html/dom` counted the cost:
// 2,411 subtests reading `undefined` where a string belongs, 665 where a
// boolean does and 576 where a number does.
//
// TWO THINGS CHANGED TOGETHER, and they are one change. The rules are a table,
// and the table's first column is an INTERFACE - so the accessors go on
// `HTMLInputElement.prototype` rather than on each input, which is both what the
// specification says and the only way ~270 of them are affordable. Building
// those prototypes is the other half of the work, and it is what
// `el instanceof HTMLBodyElement` and `eventTarget.constructor.name` ask for.

namespace {

constexpr reflected_attribute text_attr(std::string_view iface, std::string_view idl,
                                        std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::dom_string, 0, 0, 0, {},
            {},    {}};
}
// The same row with [LegacyNullToEmptyString] on its setter - see the field.
constexpr reflected_attribute legacy_text_attr(std::string_view iface, std::string_view idl,
                                               std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::dom_string, 0, 0, 0, {}, {},
            {},    true};
}
// The ARIA shape, and the only place a nullable DOMString appears: the content
// attribute is always the IDL name in another spelling, so it is spelled out
// rather than derived - `ariaAutoComplete` is `aria-autocomplete` and
// `ariaBrailleRoleDescription` is `aria-brailleroledescription`, and no rule
// relates the two.
constexpr reflected_attribute aria_attr(std::string_view idl, std::string_view content) {
    return {"Element", idl, content, reflect_type::nullable_dom_string, 0, 0, 0, {}, {}, {}};
}
// AN ENUMERATED ARIA ATTRIBUTE: nullable, with keywords and an invalid value
// default that may be null - spelt as an empty string_view, which no keyword
// is. Twenty of them, from w3c/aria#2484, which is what
// `aria-attribute-reflection-enumerated.tentative.html` measures. NO MISSING
// VALUE DEFAULT, deliberately: that file's table names one for thirteen rows
// (`ariaBusy` absent is "false") and then expects null after `el.ariaBusy =
// null` has removed the attribute, and the stable aria-attribute-reflection
// .html expects the null too - so absent is null, and the thirteen "IDL get
// with DOM attribute unset" subtests are the ones given up.
constexpr reflected_attribute aria_enum_attr(std::string_view idl, std::string_view content,
                                             std::string_view keywords, std::string_view invalid) {
    return {"Element", idl, content, reflect_type::nullable_enumerated, 0, 0, 0,
            keywords,  {},  invalid};
}
constexpr reflected_attribute url_attr(std::string_view iface, std::string_view idl,
                                       std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::url, 0, 0, 0, {}, {}, {}};
}
constexpr reflected_attribute bool_attr(std::string_view iface, std::string_view idl,
                                        std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::boolean, 0, 0, 0, {},
            {},    {}};
}
constexpr reflected_attribute long_attr(std::string_view iface, std::string_view idl,
                                        long long fallback = 0, std::string_view content = {}) {
    return {
        iface, idl, content.empty() ? idl : content, reflect_type::signed_long, fallback, 0, 0, {},
        {},    {}};
}
constexpr reflected_attribute ulong_attr(std::string_view iface, std::string_view idl,
                                         long long fallback = 0, std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::unsigned_long,
            fallback,
            0,
            0,
            {},
            {},
            {}};
}
constexpr reflected_attribute limited_long_attr(std::string_view iface, std::string_view idl,
                                                std::string_view content = {}) {
    return {iface, idl, content.empty() ? idl : content, reflect_type::limited_long, -1, 0, 0, {},
            {},    {}};
}
constexpr reflected_attribute limited_ulong_attr(std::string_view iface, std::string_view idl,
                                                 long long fallback,
                                                 std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::limited_unsigned_long,
            fallback,
            0,
            0,
            {},
            {},
            {}};
}
constexpr reflected_attribute fallback_ulong_attr(std::string_view iface, std::string_view idl,
                                                  long long fallback,
                                                  std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::unsigned_long_fallback,
            fallback,
            0,
            0,
            {},
            {},
            {}};
}
constexpr reflected_attribute clamped_attr(std::string_view iface, std::string_view idl,
                                           long long fallback, long long low, long long high,
                                           std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::clamped_unsigned_long,
            fallback,
            low,
            high,
            {},
            {},
            {}};
}
constexpr reflected_attribute enum_attr(std::string_view iface, std::string_view idl,
                                        std::string_view keywords, std::string_view missing,
                                        std::string_view invalid, std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::enumerated,
            0,
            0,
            0,
            keywords,
            missing,
            invalid};
}
// The nullable spelling of the same rule, for the CORS rows: no `missing`
// column because the missing value default is null, and an invalid value
// default that names a keyword. aria_enum_attr above is the general form.
constexpr reflected_attribute nullable_enum_attr(std::string_view iface, std::string_view idl,
                                                 std::string_view keywords,
                                                 std::string_view invalid,
                                                 std::string_view content = {}) {
    return {iface,
            idl,
            content.empty() ? idl : content,
            reflect_type::nullable_enumerated,
            0,
            0,
            0,
            keywords,
            {},
            invalid};
}

// The keyword sets that appear on more than one interface, named once so the
// table cannot spell one of them differently from the other.
constexpr std::string_view referrer_keywords =
    "no-referrer no-referrer-when-downgrade same-origin origin strict-origin "
    "origin-when-cross-origin strict-origin-when-cross-origin unsafe-url";
// CORS, which four interfaces share and which is the one non-tentative user of
// the nullable enumerated type: absent is `null`, `anonymous` and
// `use-credentials` are the keywords, and anything else - including the empty
// string, which is what `<img crossorigin>` parses to - is `anonymous`.
constexpr std::string_view cors_keywords = "anonymous use-credentials";
constexpr std::string_view enctype_keywords =
    "application/x-www-form-urlencoded multipart/form-data text/plain";
constexpr std::string_view default_enctype = "application/x-www-form-urlencoded";

// THE TABLE. Built from what the corpus tests: `html/dom/elements-*.js` in the
// WPT checkout enumerate, per element, every reflected attribute and its type,
// and the eight `reflection-*.html` files run that product.
//
// WHAT IS DELIBERATELY NOT HERE, and why:
//   * `width` and `height` on every element. The engine already answers those
//     three different ways - the canvas's drawing buffer, an <img>'s decoded
//     size, and a generic numeric accessor for any element carrying either
//     attribute - all as OWN accessors on the wrapper, which shadow anything on
//     a prototype. A row here would be dead code that reads as if it worked.
//   * `value` on a control and `src` on an <img>, for the same reason: both
//     have live own accessors, and a control's value is not its attribute.
//   * `relList`, `sandbox`, `output.htmlFor`, `link.sizes` - the token lists.
//     `classList` exists as its own object; the rest need a real DOMTokenList,
//     which is an object type rather than a table row.
//   * `document.dir`, which is on the document object rather than on an element
//     interface.
//   * `meter`'s six doubles and `progress.max`: `limited double` is a type
//     nothing else uses and the elements have no behaviour behind it here.
constexpr reflected_attribute reflection_table[] = {
    // --- Element: on everything, including SVG and a page-invented namespace
    text_attr("Element", "id"),
    text_attr("Element", "className", "class"),
    text_attr("Element", "slot"),

    // --- ARIA, WHICH IS ALSO ON EVERYTHING and is the same six lines of rule
    // --- applied forty-one more times.
    //
    // `role` and every `aria-*` content attribute reflect as an IDL attribute
    // on Element (ARIA 1.3 §9, "Reflection"), and they are NULLABLE where every
    // row above is not: an absent one reads `null` rather than "", and writing
    // `null` or `undefined` REMOVES it. `aria-attribute-reflection.html` runs
    // `testNullable` on every one of them, so a row that answered "" would fail
    // its own subtest twice over.
    //
    // THE ELEMENT-VALUED ONES ARE NOT HERE, on purpose. `ariaLabelledByElements`
    // and its five siblings reflect an IDREF list as an array of ELEMENTS
    // rather than as a string, which needs an explicit-set store on the element
    // and a live lookup per read - a different mechanism, not a different row,
    // and `aria-element-reflection*.html` is what measures it. The strings are
    // a table and the table is what is affordable.
    aria_attr("role", "role"),
    aria_enum_attr("ariaAtomic", "aria-atomic", "true false", "false"),
    aria_enum_attr("ariaAutoComplete", "aria-autocomplete", "inline list both none", "none"),
    aria_attr("ariaBrailleLabel", "aria-braillelabel"),
    aria_attr("ariaBrailleRoleDescription", "aria-brailleroledescription"),
    aria_enum_attr("ariaBusy", "aria-busy", "true false", "false"),
    aria_enum_attr("ariaChecked", "aria-checked", "true false mixed", ""),
    aria_attr("ariaColCount", "aria-colcount"),
    aria_attr("ariaColIndex", "aria-colindex"),
    aria_attr("ariaColIndexText", "aria-colindextext"),
    aria_attr("ariaColSpan", "aria-colspan"),
    aria_enum_attr("ariaCurrent", "aria-current", "page step location date time true false",
                   "true"),
    aria_attr("ariaDescription", "aria-description"),
    aria_enum_attr("ariaDisabled", "aria-disabled", "true false", "false"),
    aria_enum_attr("ariaExpanded", "aria-expanded", "true false", ""),
    aria_enum_attr("ariaHasPopup", "aria-haspopup", "true false menu dialog listbox tree grid",
                   "false"),
    aria_enum_attr("ariaHidden", "aria-hidden", "true false", "false"),
    aria_enum_attr("ariaInvalid", "aria-invalid", "true false spelling grammar", "true"),
    aria_attr("ariaKeyShortcuts", "aria-keyshortcuts"),
    aria_attr("ariaLabel", "aria-label"),
    aria_attr("ariaLevel", "aria-level"),
    aria_enum_attr("ariaLive", "aria-live", "polite assertive off", "off"),
    aria_enum_attr("ariaModal", "aria-modal", "true false", "false"),
    aria_enum_attr("ariaMultiLine", "aria-multiline", "true false", "false"),
    aria_enum_attr("ariaMultiSelectable", "aria-multiselectable", "true false", "false"),
    aria_enum_attr("ariaOrientation", "aria-orientation", "horizontal vertical", ""),
    aria_attr("ariaPlaceholder", "aria-placeholder"),
    aria_attr("ariaPosInSet", "aria-posinset"),
    aria_enum_attr("ariaPressed", "aria-pressed", "true false mixed", ""),
    aria_enum_attr("ariaReadOnly", "aria-readonly", "true false", "false"),
    aria_attr("ariaRelevant", "aria-relevant"),
    aria_enum_attr("ariaRequired", "aria-required", "true false", "false"),
    aria_attr("ariaRoleDescription", "aria-roledescription"),
    aria_attr("ariaRowCount", "aria-rowcount"),
    aria_attr("ariaRowIndex", "aria-rowindex"),
    aria_attr("ariaRowIndexText", "aria-rowindextext"),
    aria_attr("ariaRowSpan", "aria-rowspan"),
    aria_enum_attr("ariaSelected", "aria-selected", "true false", ""),
    aria_attr("ariaSetSize", "aria-setsize"),
    aria_enum_attr("ariaSort", "aria-sort", "ascending descending other none", "none"),
    aria_attr("ariaValueMax", "aria-valuemax"),
    aria_attr("ariaValueMin", "aria-valuemin"),
    aria_attr("ariaValueNow", "aria-valuenow"),
    aria_attr("ariaValueText", "aria-valuetext"),

    // --- HTMLElement: the global attributes, which the corpus tests once per
    // --- element and which are therefore worth more than any other rows here.
    text_attr("HTMLElement", "title"),
    text_attr("HTMLElement", "lang"),
    text_attr("HTMLElement", "accessKey", "accesskey"),
    {"HTMLElement", "nonce", "nonce", reflect_type::cryptographic_nonce, 0, 0, 0, {}, {}, {}},
    enum_attr("HTMLElement", "dir", "ltr rtl auto", "", ""),
    enum_attr("HTMLElement", "enterKeyHint", "enter done go next previous search send", "", "",
              "enterkeyhint"),
    enum_attr("HTMLElement", "inputMode", "none text tel url email numeric decimal search", "", "",
              "inputmode"),
    bool_attr("HTMLElement", "autofocus"),
    bool_attr("HTMLElement", "hidden"),
    // The specification's default here is "0 or -1 depending on whether the
    // element is focusable", which is a SHOULD and which the corpus explicitly
    // declines to test. 0 is what a focusable element reports.
    long_attr("HTMLElement", "tabIndex", 0, "tabindex"),

    // --- text-level semantics
    text_attr("HTMLAnchorElement", "target"),
    text_attr("HTMLAnchorElement", "download"),
    text_attr("HTMLAnchorElement", "ping"),
    text_attr("HTMLAnchorElement", "rel"),
    text_attr("HTMLAnchorElement", "hreflang"),
    text_attr("HTMLAnchorElement", "type"),
    text_attr("HTMLAnchorElement", "coords"),
    text_attr("HTMLAnchorElement", "charset"),
    text_attr("HTMLAnchorElement", "name"),
    text_attr("HTMLAnchorElement", "rev"),
    text_attr("HTMLAnchorElement", "shape"),
    enum_attr("HTMLAnchorElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    url_attr("HTMLAnchorElement", "href"),
    url_attr("HTMLQuoteElement", "cite"),
    text_attr("HTMLDataElement", "value"),
    text_attr("HTMLTimeElement", "dateTime", "datetime"),
    text_attr("HTMLBRElement", "clear"),

    // --- grouping content
    text_attr("HTMLParagraphElement", "align"),
    text_attr("HTMLHRElement", "align"),
    text_attr("HTMLHRElement", "color"),
    text_attr("HTMLHRElement", "size"),
    text_attr("HTMLHRElement", "width"),
    long_attr("HTMLPreElement", "width"),
    bool_attr("HTMLHRElement", "noShade", "noshade"),
    bool_attr("HTMLOListElement", "reversed"),
    bool_attr("HTMLOListElement", "compact"),
    long_attr("HTMLOListElement", "start", 1),
    text_attr("HTMLOListElement", "type"),
    bool_attr("HTMLUListElement", "compact"),
    text_attr("HTMLUListElement", "type"),
    long_attr("HTMLLIElement", "value"),
    text_attr("HTMLLIElement", "type"),
    bool_attr("HTMLDListElement", "compact"),
    text_attr("HTMLDivElement", "align"),
    text_attr("HTMLHeadingElement", "align"),
    bool_attr("HTMLMenuElement", "compact"),

    // --- sections
    legacy_text_attr("HTMLBodyElement", "text"),
    legacy_text_attr("HTMLBodyElement", "link"),
    legacy_text_attr("HTMLBodyElement", "vLink", "vlink"),
    legacy_text_attr("HTMLBodyElement", "aLink", "alink"),
    legacy_text_attr("HTMLBodyElement", "bgColor", "bgcolor"),
    text_attr("HTMLBodyElement", "background"),
    text_attr("HTMLHtmlElement", "version"),

    // --- metadata
    text_attr("HTMLBaseElement", "target"),
    url_attr("HTMLBaseElement", "href"),
    url_attr("HTMLLinkElement", "href"),
    nullable_enum_attr("HTMLLinkElement", "crossOrigin", cors_keywords, "anonymous", "crossorigin"),
    text_attr("HTMLLinkElement", "rel"),
    text_attr("HTMLLinkElement", "media"),
    text_attr("HTMLLinkElement", "integrity"),
    text_attr("HTMLLinkElement", "hreflang"),
    text_attr("HTMLLinkElement", "type"),
    text_attr("HTMLLinkElement", "charset"),
    text_attr("HTMLLinkElement", "rev"),
    text_attr("HTMLLinkElement", "target"),
    enum_attr("HTMLLinkElement", "as",
              "fetch audio document embed font image manifest object report script sharedworker "
              "style track video worker xslt",
              "", ""),
    enum_attr("HTMLLinkElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    text_attr("HTMLMetaElement", "name"),
    text_attr("HTMLMetaElement", "httpEquiv", "http-equiv"),
    text_attr("HTMLMetaElement", "content"),
    text_attr("HTMLMetaElement", "media"),
    text_attr("HTMLMetaElement", "scheme"),
    text_attr("HTMLStyleElement", "media"),
    text_attr("HTMLStyleElement", "type"),

    // --- scripting, edits, interactive
    url_attr("HTMLScriptElement", "src"),
    nullable_enum_attr("HTMLScriptElement", "crossOrigin", cors_keywords, "anonymous",
                       "crossorigin"),
    text_attr("HTMLScriptElement", "type"),
    text_attr("HTMLScriptElement", "charset"),
    text_attr("HTMLScriptElement", "integrity"),
    text_attr("HTMLScriptElement", "event"),
    text_attr("HTMLScriptElement", "htmlFor", "for"),
    bool_attr("HTMLScriptElement", "noModule", "nomodule"),
    bool_attr("HTMLScriptElement", "defer"),
    url_attr("HTMLModElement", "cite"),
    text_attr("HTMLModElement", "dateTime", "datetime"),
    bool_attr("HTMLDetailsElement", "open"),
    bool_attr("HTMLDialogElement", "open"),
    text_attr("HTMLSlotElement", "name"),

    // --- embedded content
    text_attr("HTMLImageElement", "alt"),
    nullable_enum_attr("HTMLImageElement", "crossOrigin", cors_keywords, "anonymous",
                       "crossorigin"),
    text_attr("HTMLImageElement", "srcset"),
    text_attr("HTMLImageElement", "useMap", "usemap"),
    text_attr("HTMLImageElement", "name"),
    text_attr("HTMLImageElement", "align"),
    legacy_text_attr("HTMLImageElement", "border"),
    bool_attr("HTMLImageElement", "isMap", "ismap"),
    ulong_attr("HTMLImageElement", "hspace"),
    ulong_attr("HTMLImageElement", "vspace"),
    url_attr("HTMLImageElement", "lowsrc"),
    url_attr("HTMLImageElement", "longDesc", "longdesc"),
    enum_attr("HTMLImageElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    enum_attr("HTMLImageElement", "decoding", "async sync auto", "auto", "auto"),
    url_attr("HTMLIFrameElement", "src"),
    text_attr("HTMLIFrameElement", "srcdoc"),
    text_attr("HTMLIFrameElement", "name"),
    text_attr("HTMLIFrameElement", "align"),
    text_attr("HTMLIFrameElement", "scrolling"),
    text_attr("HTMLIFrameElement", "frameBorder", "frameborder"),
    legacy_text_attr("HTMLIFrameElement", "marginHeight", "marginheight"),
    legacy_text_attr("HTMLIFrameElement", "marginWidth", "marginwidth"),
    text_attr("HTMLIFrameElement", "width"),
    text_attr("HTMLIFrameElement", "height"),
    url_attr("HTMLIFrameElement", "longDesc", "longdesc"),
    bool_attr("HTMLIFrameElement", "allowFullscreen", "allowfullscreen"),
    enum_attr("HTMLIFrameElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    url_attr("HTMLEmbedElement", "src"),
    text_attr("HTMLEmbedElement", "type"),
    text_attr("HTMLEmbedElement", "align"),
    text_attr("HTMLEmbedElement", "name"),
    text_attr("HTMLEmbedElement", "width"),
    text_attr("HTMLEmbedElement", "height"),
    url_attr("HTMLObjectElement", "data"),
    text_attr("HTMLObjectElement", "type"),
    text_attr("HTMLObjectElement", "name"),
    text_attr("HTMLObjectElement", "useMap", "usemap"),
    text_attr("HTMLObjectElement", "align"),
    text_attr("HTMLObjectElement", "archive"),
    text_attr("HTMLObjectElement", "code"),
    text_attr("HTMLObjectElement", "standby"),
    text_attr("HTMLObjectElement", "codeType", "codetype"),
    legacy_text_attr("HTMLObjectElement", "border"),
    bool_attr("HTMLObjectElement", "declare"),
    ulong_attr("HTMLObjectElement", "hspace"),
    ulong_attr("HTMLObjectElement", "vspace"),
    url_attr("HTMLObjectElement", "codeBase", "codebase"),
    text_attr("HTMLObjectElement", "width"),
    text_attr("HTMLObjectElement", "height"),
    text_attr("HTMLParamElement", "name"),
    text_attr("HTMLParamElement", "value"),
    text_attr("HTMLParamElement", "type"),
    text_attr("HTMLParamElement", "valueType", "valuetype"),
    url_attr("HTMLMediaElement", "src"),
    nullable_enum_attr("HTMLMediaElement", "crossOrigin", cors_keywords, "anonymous",
                       "crossorigin"),
    bool_attr("HTMLMediaElement", "autoplay"),
    bool_attr("HTMLMediaElement", "loop"),
    bool_attr("HTMLMediaElement", "controls"),
    bool_attr("HTMLMediaElement", "defaultMuted", "muted"),
    enum_attr("HTMLMediaElement", "preload", "none metadata auto", "auto", "auto"),
    enum_attr("HTMLMediaElement", "loading", "lazy eager", "eager", "eager"),
    url_attr("HTMLVideoElement", "poster"),
    bool_attr("HTMLVideoElement", "playsInline", "playsinline"),
    ulong_attr("HTMLVideoElement", "width"),
    ulong_attr("HTMLVideoElement", "height"),
    url_attr("HTMLSourceElement", "src"),
    text_attr("HTMLSourceElement", "type"),
    text_attr("HTMLSourceElement", "srcset"),
    text_attr("HTMLSourceElement", "sizes"),
    text_attr("HTMLSourceElement", "media"),
    url_attr("HTMLTrackElement", "src"),
    text_attr("HTMLTrackElement", "srclang"),
    text_attr("HTMLTrackElement", "label"),
    bool_attr("HTMLTrackElement", "default"),
    enum_attr("HTMLTrackElement", "kind", "subtitles captions descriptions chapters metadata",
              "subtitles", "metadata"),
    text_attr("HTMLMapElement", "name"),
    text_attr("HTMLAreaElement", "alt"),
    text_attr("HTMLAreaElement", "coords"),
    text_attr("HTMLAreaElement", "shape"),
    text_attr("HTMLAreaElement", "target"),
    text_attr("HTMLAreaElement", "download"),
    text_attr("HTMLAreaElement", "ping"),
    text_attr("HTMLAreaElement", "rel"),
    text_attr("HTMLAreaElement", "hreflang"),
    text_attr("HTMLAreaElement", "type"),
    bool_attr("HTMLAreaElement", "noHref", "nohref"),
    enum_attr("HTMLAreaElement", "referrerPolicy", referrer_keywords, "", "", "referrerpolicy"),
    url_attr("HTMLAreaElement", "href"),

    // --- tabular data
    text_attr("HTMLTableElement", "align"),
    text_attr("HTMLTableElement", "border"),
    text_attr("HTMLTableElement", "frame"),
    text_attr("HTMLTableElement", "rules"),
    text_attr("HTMLTableElement", "summary"),
    legacy_text_attr("HTMLTableElement", "bgColor", "bgcolor"),
    legacy_text_attr("HTMLTableElement", "cellPadding", "cellpadding"),
    legacy_text_attr("HTMLTableElement", "cellSpacing", "cellspacing"),
    text_attr("HTMLTableElement", "width"),
    text_attr("HTMLTableCaptionElement", "align"),
    text_attr("HTMLTableColElement", "align"),
    text_attr("HTMLTableColElement", "ch", "char"),
    text_attr("HTMLTableColElement", "chOff", "charoff"),
    text_attr("HTMLTableColElement", "vAlign", "valign"),
    clamped_attr("HTMLTableColElement", "span", 1, 1, 1000),
    text_attr("HTMLTableColElement", "width"),
    text_attr("HTMLTableSectionElement", "align"),
    text_attr("HTMLTableSectionElement", "ch", "char"),
    text_attr("HTMLTableSectionElement", "chOff", "charoff"),
    text_attr("HTMLTableSectionElement", "vAlign", "valign"),
    text_attr("HTMLTableRowElement", "align"),
    text_attr("HTMLTableRowElement", "ch", "char"),
    text_attr("HTMLTableRowElement", "chOff", "charoff"),
    text_attr("HTMLTableRowElement", "vAlign", "valign"),
    legacy_text_attr("HTMLTableRowElement", "bgColor", "bgcolor"),
    text_attr("HTMLTableCellElement", "headers"),
    text_attr("HTMLTableCellElement", "abbr"),
    text_attr("HTMLTableCellElement", "align"),
    text_attr("HTMLTableCellElement", "axis"),
    text_attr("HTMLTableCellElement", "ch", "char"),
    text_attr("HTMLTableCellElement", "chOff", "charoff"),
    text_attr("HTMLTableCellElement", "vAlign", "valign"),
    legacy_text_attr("HTMLTableCellElement", "bgColor", "bgcolor"),
    bool_attr("HTMLTableCellElement", "noWrap", "nowrap"),
    text_attr("HTMLTableCellElement", "width"),
    text_attr("HTMLTableCellElement", "height"),
    clamped_attr("HTMLTableCellElement", "colSpan", 1, 1, 1000, "colspan"),
    clamped_attr("HTMLTableCellElement", "rowSpan", 1, 0, 65534, "rowspan"),
    enum_attr("HTMLTableCellElement", "scope", "row col rowgroup colgroup", "", ""),

    // --- forms
    text_attr("HTMLFormElement", "acceptCharset", "accept-charset"),
    text_attr("HTMLFormElement", "name"),
    text_attr("HTMLFormElement", "target"),
    bool_attr("HTMLFormElement", "noValidate", "novalidate"),
    url_attr("HTMLFormElement", "action"),
    enum_attr("HTMLFormElement", "autocomplete", "on off", "on", "on"),
    enum_attr("HTMLFormElement", "enctype", enctype_keywords, default_enctype, default_enctype),
    enum_attr("HTMLFormElement", "encoding", enctype_keywords, default_enctype, default_enctype,
              "enctype"),
    enum_attr("HTMLFormElement", "method", "get post dialog", "get", "get"),
    bool_attr("HTMLFieldSetElement", "disabled"),
    text_attr("HTMLFieldSetElement", "name"),
    text_attr("HTMLLegendElement", "align"),
    text_attr("HTMLLabelElement", "htmlFor", "for"),
    text_attr("HTMLInputElement", "accept"),
    text_attr("HTMLInputElement", "alt"),
    text_attr("HTMLInputElement", "dirName", "dirname"),
    text_attr("HTMLInputElement", "formTarget", "formtarget"),
    text_attr("HTMLInputElement", "max"),
    text_attr("HTMLInputElement", "min"),
    text_attr("HTMLInputElement", "name"),
    text_attr("HTMLInputElement", "pattern"),
    text_attr("HTMLInputElement", "placeholder"),
    text_attr("HTMLInputElement", "step"),
    text_attr("HTMLInputElement", "align"),
    text_attr("HTMLInputElement", "useMap", "usemap"),
    text_attr("HTMLInputElement", "autocomplete"),
    ulong_attr("HTMLInputElement", "width"),
    ulong_attr("HTMLInputElement", "height"),
    text_attr("HTMLInputElement", "defaultValue", "value"),
    bool_attr("HTMLInputElement", "defaultChecked", "checked"),
    bool_attr("HTMLInputElement", "disabled"),
    bool_attr("HTMLInputElement", "multiple"),
    bool_attr("HTMLInputElement", "readOnly", "readonly"),
    bool_attr("HTMLInputElement", "required"),
    bool_attr("HTMLInputElement", "formNoValidate", "formnovalidate"),
    limited_long_attr("HTMLInputElement", "maxLength", "maxlength"),
    limited_long_attr("HTMLInputElement", "minLength", "minlength"),
    limited_ulong_attr("HTMLInputElement", "size", 20),
    url_attr("HTMLInputElement", "src"),
    url_attr("HTMLInputElement", "formAction", "formaction"),
    enum_attr("HTMLInputElement", "formEnctype", enctype_keywords, "", default_enctype,
              "formenctype"),
    enum_attr("HTMLInputElement", "formMethod", "get post", "", "get", "formmethod"),
    enum_attr("HTMLInputElement", "type",
              "hidden text search tel url email password date month week time datetime-local "
              "number range color checkbox radio file submit image reset button",
              "text", "text"),
    text_attr("HTMLButtonElement", "name"),
    text_attr("HTMLButtonElement", "value"),
    text_attr("HTMLButtonElement", "formTarget", "formtarget"),
    bool_attr("HTMLButtonElement", "disabled"),
    bool_attr("HTMLButtonElement", "formNoValidate", "formnovalidate"),
    url_attr("HTMLButtonElement", "formAction", "formaction"),
    enum_attr("HTMLButtonElement", "formEnctype", enctype_keywords, "", default_enctype,
              "formenctype"),
    enum_attr("HTMLButtonElement", "formMethod", "get post dialog", "", "get", "formmethod"),
    enum_attr("HTMLButtonElement", "type", "submit reset button", "submit", "submit"),
    text_attr("HTMLSelectElement", "name"),
    text_attr("HTMLSelectElement", "autocomplete"),
    bool_attr("HTMLSelectElement", "disabled"),
    bool_attr("HTMLSelectElement", "multiple"),
    bool_attr("HTMLSelectElement", "required"),
    ulong_attr("HTMLSelectElement", "size", 0),
    text_attr("HTMLOptGroupElement", "label"),
    bool_attr("HTMLOptGroupElement", "disabled"),
    bool_attr("HTMLOptionElement", "disabled"),
    bool_attr("HTMLOptionElement", "defaultSelected", "selected"),
    text_attr("HTMLTextAreaElement", "dirName", "dirname"),
    text_attr("HTMLTextAreaElement", "autocomplete"),
    text_attr("HTMLTextAreaElement", "name"),
    text_attr("HTMLTextAreaElement", "placeholder"),
    text_attr("HTMLTextAreaElement", "wrap"),
    bool_attr("HTMLTextAreaElement", "disabled"),
    bool_attr("HTMLTextAreaElement", "readOnly", "readonly"),
    bool_attr("HTMLTextAreaElement", "required"),
    limited_long_attr("HTMLTextAreaElement", "maxLength", "maxlength"),
    limited_long_attr("HTMLTextAreaElement", "minLength", "minlength"),
    fallback_ulong_attr("HTMLTextAreaElement", "cols", 20),
    fallback_ulong_attr("HTMLTextAreaElement", "rows", 2),
    text_attr("HTMLOutputElement", "name"),

    // --- obsolete, and still measured: the corpus has a whole file of them
    text_attr("HTMLFrameSetElement", "cols"),
    text_attr("HTMLFrameSetElement", "rows"),
    text_attr("HTMLFrameElement", "name"),
    text_attr("HTMLFrameElement", "scrolling"),
    text_attr("HTMLFrameElement", "frameBorder", "frameborder"),
    legacy_text_attr("HTMLFrameElement", "marginHeight", "marginheight"),
    legacy_text_attr("HTMLFrameElement", "marginWidth", "marginwidth"),
    bool_attr("HTMLFrameElement", "noResize", "noresize"),
    url_attr("HTMLFrameElement", "src"),
    url_attr("HTMLFrameElement", "longDesc", "longdesc"),
    bool_attr("HTMLDirectoryElement", "compact"),
    legacy_text_attr("HTMLFontElement", "color"),
    text_attr("HTMLFontElement", "face"),
    text_attr("HTMLFontElement", "size"),
    text_attr("HTMLMarqueeElement", "bgColor", "bgcolor"),
    ulong_attr("HTMLMarqueeElement", "hspace"),
    ulong_attr("HTMLMarqueeElement", "vspace"),
    ulong_attr("HTMLMarqueeElement", "scrollAmount", 6, "scrollamount"),
    ulong_attr("HTMLMarqueeElement", "scrollDelay", 85, "scrolldelay"),
    bool_attr("HTMLMarqueeElement", "trueSpeed", "truespeed"),
    text_attr("HTMLMarqueeElement", "width"),
    text_attr("HTMLMarqueeElement", "height"),
    enum_attr("HTMLMarqueeElement", "behavior", "scroll slide alternate", "scroll", "scroll"),
    enum_attr("HTMLMarqueeElement", "direction", "up right down left", "left", "left"),
};

// THE RULES FOR PARSING INTEGERS, HTML 2.4.4.1, which the numeric reflection
// types are all defined in terms of. Answers false when there is no integer
// there at all, which is what makes the attribute's default apply.
//
// The whitespace set is HTML's five characters and NOT JavaScript's -
// `core/algorithms.hpp` takes the set as a parameter for exactly this reason.
// The corpus tests a vertical tab in front of a digit among twenty other
// spacings and expects it to FAIL, because a vertical tab is not HTML
// whitespace and `\v7` is therefore not an integer.
} // namespace

namespace detail {

[[nodiscard]] bool parse_html_integer(std::string_view text, long long & out) {
    std::size_t at = 0;
    while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) { ++at; }
    long long sign = 1;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        sign = text[at] == '-' ? -1 : 1;
        ++at;
    }
    if (at >= text.size() || text[at] < '0' || text[at] > '9') { return false; }
    long long digits = 0;
    bool too_big = false;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        // CAPPED RATHER THAN WRAPPED. A page can write a hundred digits in an
        // attribute, and signed overflow is undefined behaviour rather than a
        // large number. Anything past 2^32 is out of range for every type here.
        if (digits > 4294967296LL) {
            too_big = true;
        } else {
            digits = digits * 10 + (text[at] - '0');
        }
        ++at;
    }
    if (too_big) { return false; }
    out = sign * digits;
    return true;
}

} // namespace detail

namespace {

[[nodiscard]] long long to_int32(double x) {
    const long long unsigned_value = to_uint32(x);
    return unsigned_value >= 2147483648LL ? unsigned_value - 4294967296LL : unsigned_value;
}

constexpr long long max_int32 = 2147483647;

} // namespace

namespace detail {

// ToUint32 and ToInt32 (ECMA-262 7.1.6/7.1.7), which is what WebIDL's
// `unsigned long` and `long` do to whatever a page assigns. `el.tabIndex = 1e30`
// wraps; it does not clamp and it does not throw.
[[nodiscard]] long long to_uint32(double x) {
    if (!std::isfinite(x)) { return 0; }
    double wrapped = std::fmod(std::trunc(x), 4294967296.0);
    if (wrapped < 0) { wrapped += 4294967296.0; }
    return static_cast<long long>(wrapped);
}

std::span<const reflected_attribute> reflection_rows() {
    return reflection_table;
}

} // namespace detail

// One reflected attribute's getter: read the content attribute, apply the rule
// its type names. A receiver that does not resolve to an element - a wrapper for
// something removed from the document - answers the type's default rather than
// throwing, which is what every other native in this file does.
value dom_bindings::reflected_get(context & cx, const void * row_ptr) {
    const auto & row = *static_cast<const reflected_attribute *>(row_ptr);
    const node_id id = receiver(cx);
    const auto txn = doc_->read();
    const atom name = atoms_->intern(row.content);
    const bool present = id && txn.has_attribute(id, name);
    const std::string_view raw = present ? txn.attribute_value(id, name) : std::string_view{};
    switch (row.type) {
    case reflect_type::dom_string: return cx.string(std::string{raw});
    case reflect_type::cryptographic_nonce: {
        // THE SLOT, WHILE THE ATTRIBUTE IS AS IT WAS. "Set the element's
        // [[CryptographicNonce]] to value" runs on every content attribute
        // change, and nothing here is told about one - so the slot remembers
        // the attribute text it was set beside, and an attribute that reads
        // differently now has been changed since and reloads it.
        // ponytail: a setAttribute("nonce", <the same text>) after an IDL set
        // is not seen; an attribute-change hook in attributes.cpp would be.
        const auto slot = nonce_slots_.find(pack(id));
        if (slot != nonce_slots_.end() && slot->second.second == raw) {
            return cx.string(slot->second.first);
        }
        if (slot != nonce_slots_.end()) { nonce_slots_.erase(slot); }
        return cx.string(std::string{raw});
    }
    // NULL, NOT "", and the difference is the whole of `testNullable`: an
    // absent `aria-label` has no value rather than an empty one, and a page
    // that branches on `el.ariaLabel === null` is asking whether the author
    // wrote one.
    case reflect_type::nullable_dom_string:
        return present ? cx.string(std::string{raw}) : value::null();
    case reflect_type::boolean: return value::boolean(present);
    case reflect_type::url: {
        // "If the content attribute is absent, return the empty string.
        // Otherwise parse it relative to the element's node document and return
        // the resulting URL string. If parsing fails, the value of the content
        // attribute must be returned instead."
        //
        // WHAT THIS ENGINE RESOLVES AGAINST is `location_href_` - the address
        // the browser pushed in through observe_location, which is what
        // `document.URL` and `document.baseURI` already report. It is NOT the
        // `<base href>` element: nothing here reads one, so a page carrying a
        // <base> resolves against the document's own address instead. That is a
        // real difference from a browser and it is the honest one to have -
        // inventing a base URL would be worse than using the document's.
        // `action` and `formAction` are the two rows HTML sends to the
        // document's URL when the attribute is absent (4.10.18.6, 4.10.19.6).
        if (!present) {
            const bool document_url = row.content == "action" || row.content == "formaction";
            return cx.string(document_url ? location_href_ : std::string{});
        }
        if (location_href_.empty()) { return cx.string(std::string{raw}); }
        const std::string resolved = resolve(location_href_, raw);
        return cx.string(resolved.empty() ? std::string{raw} : resolved);
    }
    case reflect_type::enumerated:
    case reflect_type::nullable_enumerated: {
        const bool nullable = row.type == reflect_type::nullable_enumerated;
        // A nullable row's invalid value default is null when the table leaves
        // it empty - `ariaChecked` set to "maybe" - and a keyword when it names
        // one - `crossOrigin` set to "maybe" is "anonymous". No keyword is the
        // empty string. Absent is null for every nullable row; see
        // aria_enum_attr for the file that wanted otherwise and then did not.
        const auto keyword_or_null = [&](std::string_view fallback) {
            return nullable && fallback.empty() ? value::null() : cx.string(std::string{fallback});
        };
        if (!present) { return nullable ? value::null() : cx.string(std::string{row.missing}); }
        // ASCII-INSENSITIVE AND NOTHING WIDER, which is the whole of the
        // corpus's interest in this line: `TRUE` is the keyword `true` and
        // U+212A KELVIN SIGN is not the letter `k`. Every keyword in the table
        // is lower case already, so the folded value IS the canonical spelling.
        const std::string folded = ascii_lower_copy(raw);
        if (lists_token(row.keywords, folded)) { return cx.string(folded); }
        // AN EMPTY VALUE IS AN INVALID ONE. It reads as if it were a state of
        // its own - `<input type="">` - and it is not: the rule is "if the
        // value matches none of the keywords, the invalid value default", and
        // an attribute that is present but empty matches none. Answering ""
        // here made `<input type="">` report "" where "text" belongs, and
        // `<track kind="">` "" where "metadata" does. The rows whose invalid
        // value default is "" - `dir`, `referrerPolicy`, `scope` - are
        // unaffected, which is why this looked right for so long.
        return keyword_or_null(row.invalid);
    }
    default: break;
    }
    long long parsed = 0;
    const bool ok = present && parse_html_integer(raw, parsed);
    long long answer = row.fallback;
    if (ok) {
        switch (row.type) {
        case reflect_type::signed_long:
            if (parsed >= -2147483648LL && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::unsigned_long:
        case reflect_type::limited_long:
            if (parsed >= 0 && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::limited_unsigned_long:
        case reflect_type::unsigned_long_fallback:
            if (parsed >= 1 && parsed <= max_int32) { answer = parsed; }
            break;
        case reflect_type::clamped_unsigned_long:
            // "If it succeeds but the value is less than min, min must be
            // returned; if greater than max, max." Only a FAILED parse falls
            // back to the default, so `<td colspan=0>` is 1 and
            // `<td colspan=x>` is 1 for two different reasons - and the parse
            // is the NON-NEGATIVE one, so `<td rowspan=-36>` fails it and is
            // the default 1 rather than the clamp's floor of 0.
            if (parsed < 0) { break; }
            answer = parsed < row.low ? row.low : (parsed > row.high ? row.high : parsed);
            break;
        default: break;
        }
    }
    return value::number(static_cast<double>(answer));
}

// ...and its setter, which is where the two types that THROW live. Answers
// undefined always: an IDL setter has no return value, and the exception is the
// only channel it has.
value dom_bindings::reflected_set(context & cx, const void * row_ptr, std::span<value> args) {
    const auto & row = *static_cast<const reflected_attribute *>(row_ptr);
    const node_id id = receiver(cx);
    if (!id) { return value::undefined(); }
    const atom name = atoms_->intern(row.content);
    const auto write = [&](std::string text) {
        (void)doc_->set_attribute(id, name, text);
        mutated();
    };
    switch (row.type) {
    case reflect_type::boolean:
        // "The content attribute must be removed if the IDL attribute is set to
        // false, and must be set to the empty string if it is set to true."
        if (!args.empty() && context::truthy(args[0])) {
            write("");
        } else {
            (void)doc_->remove_attribute(id, name);
            mutated();
        }
        return value::undefined();
    case reflect_type::nullable_dom_string:
    case reflect_type::nullable_enumerated:
        // "If the given value is null, remove the content attribute" - so
        // `el.ariaLabel = null` is a removal and not the four characters
        // "null", which is what the ToString below would have written.
        // `undefined` is the same state, which `testNullable` checks by name.
        //
        // A nullable ENUMERATED attribute writes what it is given, exactly as
        // the non-nullable one does: `img.crossOrigin = "ANONYMOUS"` stores
        // those nine capitals and the GETTER is what folds them.
        if (args.empty() || args[0].is_nullish()) {
            (void)doc_->remove_attribute(id, name);
            mutated();
            return value::undefined();
        }
        write(arg_string(cx, args, 0));
        return value::undefined();
    case reflect_type::cryptographic_nonce: {
        // The slot and NOT the attribute - see the getter. Remembered beside
        // the attribute's current text so the getter can tell a later change.
        std::string current;
        {
            const auto txn = doc_->read();
            current = std::string{txn.attribute_value(id, name)};
        }
        nonce_slots_[pack(id)] = {arg_string(cx, args, 0), std::move(current)};
        return value::undefined();
    }
    case reflect_type::dom_string:
    case reflect_type::url:
    case reflect_type::enumerated:
        // All three write the ToString of the value verbatim. An enumerated
        // attribute does NOT canonicalise on the way in - the getter is where
        // the keyword table applies - and a URL is stored as given and resolved
        // on the way out. [LegacyNullToEmptyString] is the one exception, and
        // it is for `null` ALONE: `undefined` still writes nine letters.
        write(row.null_to_empty && arg(args, 0).is_null() ? std::string{}
                                                          : arg_string(cx, args, 0));
        return value::undefined();
    default: break;
    }
    const double given = arg_number(args, 0);
    long long number =
        row.type == reflect_type::signed_long || row.type == reflect_type::limited_long
            ? to_int32(given)
            : to_uint32(given);
    switch (row.type) {
    case reflect_type::limited_long:
        // "On setting, if the value is negative, the user agent must fire an
        // INDEX_SIZE_ERR exception."
        if (number < 0) {
            throw_dom_exception(cx, "IndexSizeError",
                                std::string{row.idl} + " cannot be set to a negative number");
            return value::undefined();
        }
        break;
    case reflect_type::limited_unsigned_long:
        if (number == 0) {
            throw_dom_exception(cx, "IndexSizeError",
                                std::string{row.idl} + " cannot be set to zero");
            return value::undefined();
        }
        if (number > max_int32) { number = row.fallback; }
        break;
    case reflect_type::unsigned_long_fallback:
        if (number < 1 || number > max_int32) { number = row.fallback; }
        break;
    case reflect_type::unsigned_long:
    case reflect_type::clamped_unsigned_long:
        // A clamped attribute "behaves the same as a regular reflected unsigned
        // integer" on setting: the clamp is a GETTING rule only.
        if (number > max_int32) { number = row.fallback; }
        break;
    default: break;
    }
    write(std::to_string(number));
    return value::undefined();
}

// --- ELEMENT REFERENCES: `ariaActiveDescendantElement` AND THE SEVEN LISTS ---
//
// HTML 2.6.1's two remaining shapes, "Element" and "FrozenArray<Element>",
// which are not a table row: they carry an EXPLICITLY SET attr-element beside
// the content attribute (ARIA 1.3 §9.4). On getting, the explicit element wins
// while it is still in scope - a descendant of one of this element's
// shadow-including ancestors - else the content attribute's ID is looked up in
// this element's tree; on setting, the content attribute becomes "" and the
// element is remembered. `aria-element-reflection*.html` measures all of it.
//
// THE STATE IS ON THE WRAPPER, as the event handler slots are, because the
// wrapper is what the collector traces: the explicit reference, the content
// attribute's text as it was when it was set (a content attribute changed
// since then has superseded the reference - there is no attribute-change
// hook, so this is checked on every read), and for a list the array last
// answered, so a page comparing two reads by identity gets the same object
// while nothing changed.
namespace {

struct element_reference_row {
    std::string_view idl;
    std::string_view content;
    bool list;
};

constexpr element_reference_row element_reference_rows[] = {
    {"ariaActiveDescendantElement", "aria-activedescendant", false},
    {"ariaControlsElements", "aria-controls", true},
    {"ariaDescribedByElements", "aria-describedby", true},
    {"ariaDetailsElements", "aria-details", true},
    {"ariaErrorMessageElements", "aria-errormessage", true},
    {"ariaFlowToElements", "aria-flowto", true},
    {"ariaLabelledByElements", "aria-labelledby", true},
    {"ariaOwnsElements", "aria-owns", true},
};

[[nodiscard]] std::string explicit_slot(std::string_view idl) {
    return "__explicit_" + std::string{idl};
}
[[nodiscard]] std::string explicit_source_slot(std::string_view idl) {
    return "__explicitsrc_" + std::string{idl};
}
[[nodiscard]] std::string cached_slot(std::string_view idl) {
    return "__cached_" + std::string{idl};
}

} // namespace

void dom_bindings::install_element_reflection(context & cx) {
    const value iface = interface_prototype("Element");
    if (!iface.is_object()) { return; }
    auto * proto = static_cast<script::object_object *>(iface.as_heap());
    for (const element_reference_row & row : element_reference_rows) {
        const std::string name{row.idl};
        proto->define_accessor(
            name,
            value::object(cx.allocate<script::native_object>(
                name,
                [this, &row](context & c, std::span<value>) {
                    return element_reference_get(c, row.idl, row.content, row.list);
                })),
            value::object(cx.allocate<script::native_object>(
                name, [this, &row](context & c, std::span<value> a) {
                    element_reference_set(c, row.idl, row.content, row.list, arg(a, 0));
                    return value::undefined();
                })));
    }
}

// "Descendant of any of `element`'s shadow-including ancestors": the candidate's
// tree is this element's tree, or the tree of a host above it.
bool dom_bindings::element_reference_in_scope(const read_txn & txn, node_id element,
                                              node_id candidate) const {
    const node_id wanted = root_of_tree(txn, candidate, false);
    for (node_id root = root_of_tree(txn, element, false); root;) {
        if (root == wanted) { return true; }
        const shadow_tree * tree = shadow_tree_of(root);
        root = tree == nullptr ? node_id{} : root_of_tree(txn, tree->host, false);
    }
    return false;
}

// The first element in `element`'s tree whose ID is `id` - DOM's "get an
// element by ID" scoped to the root, which for a disconnected subtree is the
// subtree and for a shadow tree is that tree alone.
node_id dom_bindings::element_reference_by_id(const read_txn & txn, node_id element,
                                              std::string_view id) const {
    if (id.empty()) { return {}; }
    const atom id_attribute = atoms_->intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (txn.kind(at).value_or(node_kind::text) == node_kind::element &&
            txn.attribute_value(at, id_attribute) == id) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, root_of_tree(txn, element, false));
    return found;
}

value dom_bindings::element_reference_get(context & cx, std::string_view idl,
                                          std::string_view content, bool list) {
    const value self = cx.current_this();
    // THE DOCUMENT THAT OWNS THE RECEIVER answers, as every prototype method
    // shared across the realm's documents does: a createHTMLDocument's element
    // reads its own tree (aria-element-reflection.html, "Adopting element
    // keeps references").
    if (dom_bindings * owner = owner_of(self); owner != nullptr && owner != this) {
        return owner->element_reference_get(cx, idl, content, list);
    }
    const node_id id = receiver(cx);
    if (!id || !self.is_object()) { return value::null(); }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    const auto txn = doc_->read();
    const atom name = atoms_->intern(content);
    const bool present = txn.has_attribute(id, name);
    const std::string raw = present ? std::string{txn.attribute_value(id, name)} : std::string{};
    std::vector<node_id> found;
    bool answered = false;
    // The explicit reference, while the content attribute still reads as it
    // did when the reference was set.
    if (const value * held = object->find(explicit_slot(idl)); held != nullptr) {
        const value * source = object->find(explicit_source_slot(idl));
        const bool superseded =
            !present || source == nullptr || !source->is_string() || cx.to_string(*source) != raw;
        if (superseded) {
            (void)object->erase(explicit_slot(idl));
            (void)object->erase(explicit_source_slot(idl));
        } else {
            answered = true;
            // A node of ANOTHER document is never in scope, and its id means
            // nothing in this tree - so the owner is asked first.
            const auto keep = [&](value candidate) {
                if (owner_of(candidate) != this) { return; }
                const node_id node = handle_of(candidate);
                if (node && element_reference_in_scope(txn, id, node)) { found.push_back(node); }
            };
            if (held->is_array()) {
                for (const value & each :
                     static_cast<script::array_object *>(held->as_heap())->items) {
                    keep(each);
                }
            } else {
                keep(*held);
            }
        }
    }
    if (!answered) {
        if (!present) { return value::null(); }
        if (list) {
            for (const std::string_view token : split(raw)) {
                if (const node_id node = element_reference_by_id(txn, id, token)) {
                    found.push_back(node);
                }
            }
        } else if (const node_id node = element_reference_by_id(txn, id, raw)) {
            found.push_back(node);
        }
    }
    if (!list) { return found.empty() ? value::null() : wrap(cx, found.front()); }
    // THE SAME ARRAY while it would hold the same elements.
    if (const value * cached = object->find(cached_slot(idl));
        cached != nullptr && cached->is_array()) {
        const auto & items = static_cast<script::array_object *>(cached->as_heap())->items;
        bool same = items.size() == found.size();
        for (std::size_t i = 0; same && i < items.size(); ++i) {
            same = handle_of(items[i]) == found[i];
        }
        if (same) { return *cached; }
    }
    value made = cx.make_array();
    auto * items = static_cast<script::array_object *>(made.as_heap());
    for (const node_id node : found) { items->items.push_back(wrap(cx, node)); }
    object->define(cached_slot(idl), made, script::attr_none);
    return made;
}

void dom_bindings::element_reference_set(context & cx, std::string_view idl,
                                         std::string_view content, bool list, value given) {
    const value self = cx.current_this();
    if (dom_bindings * owner = owner_of(self); owner != nullptr && owner != this) {
        owner->element_reference_set(cx, idl, content, list, given);
        return;
    }
    const node_id id = receiver(cx);
    if (!id || !self.is_object()) { return; }
    auto * object = static_cast<script::object_object *>(self.as_heap());
    const atom name = atoms_->intern(content);
    // null (and undefined) removes the content attribute and forgets the
    // reference.
    if (given.is_nullish()) {
        (void)object->erase(explicit_slot(idl));
        (void)object->erase(explicit_source_slot(idl));
        (void)doc_->remove_attribute(id, name);
        mutated();
        return;
    }
    // An element of any document in the realm: one from another document is
    // accepted and simply out of scope until it is adopted.
    const auto is_element = [&](value v) {
        dom_bindings * owner = owner_of(v);
        if (owner == nullptr) { return false; }
        const node_id node = owner->handle_of(v);
        return node &&
               owner->doc_->read().kind(node).value_or(node_kind::text) == node_kind::element;
    };
    value kept = given;
    if (list) {
        if (!given.is_array()) {
            cx.throw_error("TypeError", "Failed to set '" + std::string{idl} +
                                            "': the value is not a sequence of Elements.");
            return;
        }
        // A COPY, so a page mutating the array it passed does not edit the slot.
        kept = cx.make_array();
        auto * items = static_cast<script::array_object *>(kept.as_heap());
        for (const value & each : static_cast<script::array_object *>(given.as_heap())->items) {
            if (!is_element(each)) {
                cx.throw_error("TypeError", "Failed to set '" + std::string{idl} +
                                                "': an item is not an Element.");
                return;
            }
            items->items.push_back(each);
        }
    } else if (!is_element(given)) {
        cx.throw_error("TypeError",
                       "Failed to set '" + std::string{idl} + "': the value is not an Element.");
        return;
    }
    // "Set the content attribute to the empty string" and remember the
    // reference beside the text it was set with.
    (void)doc_->set_attribute(id, name, "");
    mutated();
    object->define(explicit_slot(idl), kept, script::attr_none);
    object->define(explicit_source_slot(idl), cx.string(""), script::attr_none);
}

// --- DOUBLES: `progress.max` AND `<meter>`'s SIX ---------------------------
//
// HTML 2.6.12/2.6.13, "double" and "double limited to only positive numbers",
// over the rules for parsing floating-point number values (2.4.4.3). The
// getter answers the default when the attribute is absent or does not parse
// - or, limited, is not positive; the setter writes the number's JavaScript
// string, and a limited row leaves the attribute alone for a value that is
// not positive. NOT in the table: its types are integers and strings, and the
// six meter rows have a custom getter in the specification (each is clamped
// against the others) that this does not attempt - reflection-forms.html
// tests only their setters, which is what "customGetter" there means.
namespace {

struct double_row {
    std::string_view interface;
    std::string_view idl;
    double fallback;
    bool positive;
};

constexpr double_row double_rows[] = {
    {"HTMLProgressElement", "max", 1.0, true},   {"HTMLMeterElement", "value", 0.0, false},
    {"HTMLMeterElement", "min", 0.0, false},     {"HTMLMeterElement", "max", 0.0, false},
    {"HTMLMeterElement", "low", 0.0, false},     {"HTMLMeterElement", "high", 0.0, false},
    {"HTMLMeterElement", "optimum", 0.0, false},
};

// THE RULES FOR PARSING FLOATING-POINT NUMBER VALUES, HTML 2.4.4.3, as a
// syntax check that hands the matched text to from_chars: HTML whitespace,
// a sign, digits or a fraction, an optional fraction, an optional exponent
// with its own sign - and anything after that is ignored rather than fatal.
[[nodiscard]] bool parse_html_float(std::string_view text, double & out) {
    std::size_t at = 0;
    while (at < text.size() && html_whitespace.find(text[at]) != std::string_view::npos) { ++at; }
    std::string canonical;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        if (text[at] == '-') { canonical += '-'; }
        ++at;
    }
    const auto digit = [&](std::size_t i) {
        return i < text.size() && text[i] >= '0' && text[i] <= '9';
    };
    if (!digit(at) && !(at < text.size() && text[at] == '.' && digit(at + 1))) { return false; }
    if (!digit(at)) { canonical += '0'; }
    while (digit(at)) { canonical += text[at++]; }
    if (at < text.size() && text[at] == '.') {
        ++at;
        canonical += '.';
        if (!digit(at)) { canonical += '0'; }
        while (digit(at)) { canonical += text[at++]; }
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        std::size_t look = at + 1;
        std::string exponent = "e";
        if (look < text.size() && (text[look] == '-' || text[look] == '+')) {
            if (text[look] == '-') { exponent += '-'; }
            ++look;
        }
        if (digit(look)) {
            while (digit(look)) { exponent += text[look++]; }
            canonical += exponent;
        }
    }
    const auto result = std::from_chars(canonical.data(), canonical.data() + canonical.size(), out);
    if (result.ec != std::errc{} || !std::isfinite(out)) { return false; }
    if (out == 0) { out = 0; } // -0 is 0, as the specification's algorithm yields
    return true;
}

} // namespace

void dom_bindings::install_double_reflection(context & cx) {
    for (const double_row & row : double_rows) {
        const value iface = interface_prototype(row.interface);
        if (!iface.is_object()) { continue; }
        auto * proto = static_cast<script::object_object *>(iface.as_heap());
        const std::string name{row.idl};
        proto->define_accessor(
            name,
            value::object(cx.allocate<script::native_object>(
                name,
                [this, &row](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return value::number(row.fallback); }
                    const auto txn = doc_->read();
                    const atom attribute = atoms_->intern(row.idl);
                    double parsed = 0;
                    if (!txn.has_attribute(id, attribute) ||
                        !parse_html_float(txn.attribute_value(id, attribute), parsed) ||
                        (row.positive && parsed <= 0)) {
                        return value::number(row.fallback);
                    }
                    return value::number(parsed);
                })),
            value::object(cx.allocate<script::native_object>(
                name, [this, &row](context & c, std::span<value> a) {
                    const node_id id = receiver(c);
                    if (!id) { return value::undefined(); }
                    const double given = arg_number(a, 0);
                    // WebIDL's `double` is a finite number or a TypeError.
                    if (!std::isfinite(given)) {
                        c.throw_error("TypeError", "Failed to set '" + std::string{row.idl} +
                                                       "': the value is not a finite number.");
                        return value::undefined();
                    }
                    if (row.positive && given <= 0) { return value::undefined(); }
                    (void)doc_->set_attribute(id, atoms_->intern(row.idl),
                                              c.to_string(value::number(given)));
                    mutated();
                    return value::undefined();
                })));
    }
}

// --- `control.form`: THE FORM OWNER, HTML 4.10.17.3 ---------------------------
//
// The `form` attribute names a form by id in the element's tree; otherwise the
// nearest form ancestor; otherwise null. Read at each get rather than kept as
// state - "reset the form owner" runs on every insertion in the specification,
// and a walk up is what it amounts to. Node-appendChild-script-and-button-
// from-div.html reads it from a script that ran the moment its div connected.
void dom_bindings::install_form_owner(context & cx) {
    for (const char * which :
         {"HTMLButtonElement", "HTMLFieldSetElement", "HTMLInputElement", "HTMLObjectElement",
          "HTMLOutputElement", "HTMLSelectElement", "HTMLTextAreaElement"}) {
        const value iface = interface_prototype(which);
        if (!iface.is_object()) { continue; }
        auto * proto = static_cast<script::object_object *>(iface.as_heap());
        proto->define_accessor(
            "form",
            value::object(cx.allocate<script::native_object>(
                "form",
                [this](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return value::null(); }
                    node_id owner;
                    {
                        const auto txn = doc_->read();
                        const std::string_view named =
                            txn.attribute_value(id, atoms_->intern("form"));
                        if (!named.empty()) {
                            const node_id found = find_by_id(std::string{named});
                            if (found && txn.element_ns(found) == node_ns::html &&
                                txn.local_name(found) == "form" &&
                                root_of_tree(txn, found, false) == root_of_tree(txn, id, false)) {
                                owner = found;
                            }
                        } else {
                            for (node_id at = txn.parent(id); at; at = txn.parent(at)) {
                                if (txn.element_ns(at) == node_ns::html &&
                                    txn.local_name(at) == "form") {
                                    owner = at;
                                    break;
                                }
                            }
                        }
                    }
                    return owner ? wrap(c, owner) : value::null();
                })),
            value::undefined());
    }
}

// --- `option.label` AND `option.value`: THE ATTRIBUTE, ELSE THE TEXT ---------
//
// HTML 4.10.10: both read the content attribute when it is present and the
// option's text otherwise - its descendant text, stripped and collapsed -
// and both write the attribute. Not a table row because of that fallback.
void dom_bindings::install_option_reflection(context & cx) {
    const value iface = interface_prototype("HTMLOptionElement");
    if (!iface.is_object()) { return; }
    auto * proto = static_cast<script::object_object *>(iface.as_heap());
    for (const char * name : {"label", "value"}) {
        proto->define_accessor(
            name,
            value::object(cx.allocate<script::native_object>(
                name,
                [this, name](context & c, std::span<value>) {
                    const node_id id = receiver(c);
                    if (!id) { return c.string(""); }
                    {
                        const auto txn = doc_->read();
                        const atom attribute = atoms_->intern(name);
                        if (txn.has_attribute(id, attribute)) {
                            return c.string(std::string{txn.attribute_value(id, attribute)});
                        }
                    }
                    // "Strip and collapse ASCII whitespace" over the text.
                    std::string collapsed;
                    for (const char each : text_of(id)) {
                        const bool space = html_whitespace.find(each) != std::string_view::npos;
                        if (space && (collapsed.empty() || collapsed.back() == ' ')) { continue; }
                        collapsed += space ? ' ' : each;
                    }
                    if (!collapsed.empty() && collapsed.back() == ' ') { collapsed.pop_back(); }
                    return c.string(collapsed);
                })),
            value::object(cx.allocate<script::native_object>(
                name, [this, name](context & c, std::span<value> a) {
                    if (const node_id id = receiver(c)) {
                        (void)doc_->set_attribute(id, atoms_->intern(name), arg_string(c, a, 0));
                        mutated();
                    }
                    return value::undefined();
                })));
    }
}

} // namespace ctbrowser::shell
