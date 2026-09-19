#include "internal.hpp"
#include <charconv>

namespace ctbrowser::shell {

using namespace detail;

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
    bool_attr("HTMLSelectElement", "disabled"),
    bool_attr("HTMLSelectElement", "multiple"),
    bool_attr("HTMLSelectElement", "required"),
    ulong_attr("HTMLSelectElement", "size", 0),
    text_attr("HTMLOptGroupElement", "label"),
    bool_attr("HTMLOptGroupElement", "disabled"),
    bool_attr("HTMLOptionElement", "disabled"),
    bool_attr("HTMLOptionElement", "defaultSelected", "selected"),
    text_attr("HTMLTextAreaElement", "dirName", "dirname"),
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
std::span<const reflected_attribute> reflection_rows() {
    return reflection_table;
}
} // namespace detail

} // namespace ctbrowser::shell
