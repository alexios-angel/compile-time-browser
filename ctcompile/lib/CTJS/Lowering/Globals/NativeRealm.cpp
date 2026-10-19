//===- NativeRealm.cpp - Globals a native program's realm provides --------===//
//
// nativeRealmProvides (declared in ctcompile/CTJS/Lowering/NativeRealm.h). The
// standard library half is read from a fresh script VM realm, so it follows the
// VM. The host half is a list: the native lowering cannot construct the
// browser shell, so the window names it installs are spelled out here. The
// list was taken from the names ctbrowser/lib/Shell installs (define_global,
// define_builtin_global, define_native, interface_of and the element interface
// table) and widened with common web platform names; when the shell installs a
// new global, add it here.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/Lowering/NativeRealm.h"

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/vm.hpp>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"

namespace ctcompile::ctjs {
namespace {

/// Window names the ctbrowser host installs, and common web platform names.
constexpr llvm::StringLiteral hostNames[] = {
    // Window and its aliases, properties and operations.
    "window", "self", "globalThis", "frames", "parent", "top", "opener", "frameElement", "document",
    "location", "history", "navigator", "clientInformation", "screen", "performance", "crypto",
    "customElements", "console", "event", "name", "status", "closed", "length", "origin",
    "isSecureContext", "crossOriginIsolated", "visualViewport", "innerWidth", "innerHeight",
    "outerWidth", "outerHeight", "devicePixelRatio", "scrollX", "scrollY", "pageXOffset",
    "pageYOffset", "screenX", "screenY", "screenLeft", "screenTop", "localStorage",
    "sessionStorage", "indexedDB", "caches", "speechSynthesis", "addEventListener",
    "removeEventListener", "dispatchEvent", "getComputedStyle", "getSelection", "matchMedia",
    "open", "close", "stop", "focus", "blur", "print", "alert", "confirm", "prompt", "postMessage",
    "scroll", "scrollTo", "scrollBy", "moveTo", "moveBy", "resizeTo", "resizeBy", "setTimeout",
    "setInterval", "clearTimeout", "clearInterval", "requestAnimationFrame", "cancelAnimationFrame",
    "requestIdleCallback", "cancelIdleCallback", "queueMicrotask", "structuredClone", "reportError",
    "atob", "btoa", "fetch", "createImageBitmap", "loadImage", "imageWidth", "imageHeight",
    "webkitURL", "__ctbrowser_from_async",
    // Interfaces and constructors.
    "AbortController", "AbortSignal", "AbstractRange", "Animation", "AnimationEvent", "Attr",
    "Audio", "BeforeUnloadEvent", "Blob", "BroadcastChannel", "CDATASection", "CSS",
    "CSSConditionRule", "CSSContainerRule", "CSSCounterStyleRule", "CSSFontFaceRule",
    "CSSFontFeatureValuesMap", "CSSFontFeatureValuesRule", "CSSGroupingRule", "CSSImportRule",
    "CSSKeyframeRule", "CSSKeyframesRule", "CSSLayerBlockRule", "CSSLayerStatementRule",
    "CSSMediaRule", "CSSNamespaceRule", "CSSPageRule", "CSSRule", "CSSRuleList", "CSSScopeRule",
    "CSSStartingStyleRule", "CSSStyleDeclaration", "CSSStyleRule", "CSSStyleSheet",
    "CSSSupportsRule", "CanvasGradient", "CanvasPattern", "CanvasRenderingContext2D",
    "CharacterData", "ClipboardEvent", "CloseEvent", "Comment", "CompositionEvent", "Crypto",
    "CustomElementRegistry", "CustomEvent", "CustomStateSet", "DOMException", "DOMImplementation",
    "DOMMatrix", "DOMMatrixReadOnly", "DOMParser", "DOMPoint", "DOMPointReadOnly", "DOMRect",
    "DOMRectList", "DOMRectReadOnly", "DOMStringMap", "DOMTokenList", "DataTransfer",
    "DataTransferItemList", "DeviceMotionEvent", "DeviceOrientationEvent", "Document",
    "DocumentFragment", "DocumentTimeline", "DocumentType", "DragEvent", "Element",
    "ElementInternals", "ErrorEvent", "Event", "EventSource", "EventTarget", "File", "FileList",
    "FileReader", "FocusEvent", "FontFace", "FormData", "FormDataEvent", "HTMLAllCollection",
    "HTMLAnchorElement", "HTMLAreaElement", "HTMLAudioElement", "HTMLBRElement", "HTMLBaseElement",
    "HTMLBodyElement", "HTMLButtonElement", "HTMLCanvasElement", "HTMLCollection",
    "HTMLDListElement", "HTMLDataElement", "HTMLDataListElement", "HTMLDetailsElement",
    "HTMLDialogElement", "HTMLDirectoryElement", "HTMLDivElement", "HTMLDocument", "HTMLElement",
    "HTMLEmbedElement", "HTMLFieldSetElement", "HTMLFontElement", "HTMLFormControlsCollection",
    "HTMLFormElement", "HTMLFrameElement", "HTMLFrameSetElement", "HTMLHRElement",
    "HTMLHeadElement", "HTMLHeadingElement", "HTMLHtmlElement", "HTMLIFrameElement",
    "HTMLImageElement", "HTMLInputElement", "HTMLLIElement", "HTMLLabelElement",
    "HTMLLegendElement", "HTMLLinkElement", "HTMLMapElement", "HTMLMarqueeElement",
    "HTMLMediaElement", "HTMLMenuElement", "HTMLMetaElement", "HTMLMeterElement", "HTMLModElement",
    "HTMLOListElement", "HTMLObjectElement", "HTMLOptGroupElement", "HTMLOptionElement",
    "HTMLOptionsCollection", "HTMLOutputElement", "HTMLParagraphElement", "HTMLParamElement",
    "HTMLPictureElement", "HTMLPreElement", "HTMLProgressElement", "HTMLQuoteElement",
    "HTMLScriptElement", "HTMLSelectElement", "HTMLSlotElement", "HTMLSourceElement",
    "HTMLSpanElement", "HTMLStyleElement", "HTMLTableCaptionElement", "HTMLTableCellElement",
    "HTMLTableColElement", "HTMLTableElement", "HTMLTableRowElement", "HTMLTableSectionElement",
    "HTMLTemplateElement", "HTMLTextAreaElement", "HTMLTimeElement", "HTMLTitleElement",
    "HTMLTrackElement", "HTMLUListElement", "HTMLUnknownElement", "HTMLVideoElement",
    "HashChangeEvent", "Headers", "History", "IdleDeadline", "Image", "ImageBitmap", "ImageData",
    "InputEvent", "IntersectionObserver", "IntersectionObserverEntry", "KeyboardEvent",
    "KeyframeEffect", "Location", "MathMLElement", "MediaList", "MediaQueryList",
    "MediaQueryListEvent", "MessageChannel", "MessageEvent", "MessagePort", "MouseEvent",
    "MutationObserver", "MutationRecord", "NamedNodeMap", "Navigator", "Node", "NodeFilter",
    "NodeIterator", "NodeList", "Notification", "OffscreenCanvas", "Option", "PageTransitionEvent",
    "Path2D", "Performance", "PerformanceEntry", "PointerEvent", "PopStateEvent",
    "ProcessingInstruction", "ProgressEvent", "PromiseRejectionEvent", "RadioNodeList", "Range",
    "Request", "ResizeObserver", "ResizeObserverEntry", "Response", "SVGElement",
    "SVGGraphicsElement", "SVGSVGElement", "Screen", "Selection", "ShadowRoot", "StaticRange",
    "Storage", "StorageEvent", "StyleSheet", "StyleSheetList", "SubmitEvent", "Text", "TextDecoder",
    "TextEncoder", "TextEvent", "TextMetrics", "ToggleEvent", "Touch", "TouchEvent", "TouchList",
    "TransitionEvent", "TreeWalker", "UIEvent", "URL", "URLSearchParams", "ValidityState",
    "WebGL2RenderingContext", "WebGLRenderingContext", "WebSocket", "WheelEvent", "Window",
    "Worker", "XMLDocument", "XMLHttpRequest", "XMLHttpRequestEventTarget", "XMLSerializer", "Intl",
    "WebAssembly"};

/// The script VM's standard library: every global a fresh realm holds.
llvm::StringSet<> standardLibrary() {
    llvm::StringSet<> names;
    ctbrowser::script::context cx;
    ctbrowser::script::install_builtins(cx);
    for (const auto & [name, value] : cx.globals()) {
        (void)value;
        names.insert(name);
    }
    return names;
}

} // namespace

bool nativeRealmProvides(llvm::StringRef name) {
    static const llvm::StringSet<> standard = standardLibrary();
    static const llvm::StringSet<> host(llvm::from_range, hostNames);
    // Event handler IDL attributes (`onload`, `onclick`, ...): the window has
    // one per event type, and the list of event types is open-ended.
    const bool handler =
        name.starts_with("on") && name.size() > 2 &&
        llvm::all_of(name.drop_front(2), [](char c) { return c >= 'a' && c <= 'z'; });
    return handler || standard.contains(name) || host.contains(name);
}

bool nativeHostMethodKeepsGlobals(llvm::StringRef name) {
    static const llvm::StringSet<> inert = {"addEventListener",
                                            "removeEventListener",
                                            "getComputedStyle",
                                            "getSelection",
                                            "matchMedia",
                                            "scroll",
                                            "scrollTo",
                                            "scrollBy",
                                            "requestAnimationFrame",
                                            "cancelAnimationFrame",
                                            "clearTimeout",
                                            "clearInterval",
                                            "atob",
                                            "btoa"};
    return inert.contains(name);
}

} // namespace ctcompile::ctjs
