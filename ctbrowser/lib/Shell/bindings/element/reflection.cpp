// dom_bindings - reflection: the table of reflected IDL attributes, HTML 2.6,
// and the one getter and one setter every row is answered by.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

#include "internal.hpp"

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
// The ARIA shape, and the only place a nullable DOMString appears: the content
// attribute is always the IDL name in another spelling, so it is spelled out
// rather than derived - `ariaAutoComplete` is `aria-autocomplete` and
// `ariaBrailleRoleDescription` is `aria-brailleroledescription`, and no rule
// relates the two.
constexpr reflected_attribute aria_attr(std::string_view idl, std::string_view content) {
    return {"Element", idl, content, reflect_type::nullable_dom_string, 0, 0, 0, {}, {}, {}};
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
// The nullable spelling of the same rule. There is no `missing` column because
// the missing value default IS null - that is what makes the type - and the
// invalid value default is always a keyword.
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
    aria_attr("ariaAtomic", "aria-atomic"),
    aria_attr("ariaAutoComplete", "aria-autocomplete"),
    aria_attr("ariaBrailleLabel", "aria-braillelabel"),
    aria_attr("ariaBrailleRoleDescription", "aria-brailleroledescription"),
    aria_attr("ariaBusy", "aria-busy"),
    aria_attr("ariaChecked", "aria-checked"),
    aria_attr("ariaColCount", "aria-colcount"),
    aria_attr("ariaColIndex", "aria-colindex"),
    aria_attr("ariaColIndexText", "aria-colindextext"),
    aria_attr("ariaColSpan", "aria-colspan"),
    aria_attr("ariaCurrent", "aria-current"),
    aria_attr("ariaDescription", "aria-description"),
    aria_attr("ariaDisabled", "aria-disabled"),
    aria_attr("ariaExpanded", "aria-expanded"),
    aria_attr("ariaHasPopup", "aria-haspopup"),
    aria_attr("ariaHidden", "aria-hidden"),
    aria_attr("ariaInvalid", "aria-invalid"),
    aria_attr("ariaKeyShortcuts", "aria-keyshortcuts"),
    aria_attr("ariaLabel", "aria-label"),
    aria_attr("ariaLevel", "aria-level"),
    aria_attr("ariaLive", "aria-live"),
    aria_attr("ariaModal", "aria-modal"),
    aria_attr("ariaMultiLine", "aria-multiline"),
    aria_attr("ariaMultiSelectable", "aria-multiselectable"),
    aria_attr("ariaOrientation", "aria-orientation"),
    aria_attr("ariaPlaceholder", "aria-placeholder"),
    aria_attr("ariaPosInSet", "aria-posinset"),
    aria_attr("ariaPressed", "aria-pressed"),
    aria_attr("ariaReadOnly", "aria-readonly"),
    aria_attr("ariaRelevant", "aria-relevant"),
    aria_attr("ariaRequired", "aria-required"),
    aria_attr("ariaRoleDescription", "aria-roledescription"),
    aria_attr("ariaRowCount", "aria-rowcount"),
    aria_attr("ariaRowIndex", "aria-rowindex"),
    aria_attr("ariaRowIndexText", "aria-rowindextext"),
    aria_attr("ariaRowSpan", "aria-rowspan"),
    aria_attr("ariaSelected", "aria-selected"),
    aria_attr("ariaSetSize", "aria-setsize"),
    aria_attr("ariaSort", "aria-sort"),
    aria_attr("ariaValueMax", "aria-valuemax"),
    aria_attr("ariaValueMin", "aria-valuemin"),
    aria_attr("ariaValueNow", "aria-valuenow"),
    aria_attr("ariaValueText", "aria-valuetext"),

    // --- HTMLElement: the global attributes, which the corpus tests once per
    // --- element and which are therefore worth more than any other rows here.
    text_attr("HTMLElement", "title"),
    text_attr("HTMLElement", "lang"),
    text_attr("HTMLElement", "accessKey", "accesskey"),
    text_attr("HTMLElement", "nonce"),
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
    text_attr("HTMLBodyElement", "text"),
    text_attr("HTMLBodyElement", "link"),
    text_attr("HTMLBodyElement", "vLink", "vlink"),
    text_attr("HTMLBodyElement", "aLink", "alink"),
    text_attr("HTMLBodyElement", "bgColor", "bgcolor"),
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
    text_attr("HTMLImageElement", "border"),
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
    text_attr("HTMLIFrameElement", "marginHeight", "marginheight"),
    text_attr("HTMLIFrameElement", "marginWidth", "marginwidth"),
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
    text_attr("HTMLObjectElement", "type"),
    text_attr("HTMLObjectElement", "name"),
    text_attr("HTMLObjectElement", "useMap", "usemap"),
    text_attr("HTMLObjectElement", "align"),
    text_attr("HTMLObjectElement", "archive"),
    text_attr("HTMLObjectElement", "code"),
    text_attr("HTMLObjectElement", "standby"),
    text_attr("HTMLObjectElement", "codeType", "codetype"),
    text_attr("HTMLObjectElement", "border"),
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
    text_attr("HTMLTableElement", "bgColor", "bgcolor"),
    text_attr("HTMLTableElement", "cellPadding", "cellpadding"),
    text_attr("HTMLTableElement", "cellSpacing", "cellspacing"),
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
    text_attr("HTMLTableRowElement", "bgColor", "bgcolor"),
    text_attr("HTMLTableCellElement", "headers"),
    text_attr("HTMLTableCellElement", "abbr"),
    text_attr("HTMLTableCellElement", "align"),
    text_attr("HTMLTableCellElement", "axis"),
    text_attr("HTMLTableCellElement", "ch", "char"),
    text_attr("HTMLTableCellElement", "chOff", "charoff"),
    text_attr("HTMLTableCellElement", "vAlign", "valign"),
    text_attr("HTMLTableCellElement", "bgColor", "bgcolor"),
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
    text_attr("HTMLFrameElement", "marginHeight", "marginheight"),
    text_attr("HTMLFrameElement", "marginWidth", "marginwidth"),
    bool_attr("HTMLFrameElement", "noResize", "noresize"),
    url_attr("HTMLFrameElement", "src"),
    url_attr("HTMLFrameElement", "longDesc", "longdesc"),
    bool_attr("HTMLDirectoryElement", "compact"),
    text_attr("HTMLFontElement", "color"),
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
        if (!present) { return cx.string(""); }
        if (location_href_.empty()) { return cx.string(std::string{raw}); }
        const std::string resolved = resolve(location_href_, raw);
        return cx.string(resolved.empty() ? std::string{raw} : resolved);
    }
    case reflect_type::enumerated:
    case reflect_type::nullable_enumerated: {
        const bool nullable = row.type == reflect_type::nullable_enumerated;
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
        return cx.string(std::string{row.invalid});
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
            // `<td colspan=x>` is 1 for two different reasons.
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
    case reflect_type::dom_string:
    case reflect_type::url:
    case reflect_type::enumerated:
        // All three write the ToString of the value verbatim. An enumerated
        // attribute does NOT canonicalise on the way in - the getter is where
        // the keyword table applies - and a URL is stored as given and resolved
        // on the way out.
        write(arg_string(cx, args, 0));
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

} // namespace ctbrowser::shell
