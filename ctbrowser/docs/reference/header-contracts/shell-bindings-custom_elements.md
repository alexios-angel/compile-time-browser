# Shell Bindings Custom_Elements contracts

<a id="contract-1"></a>

`[[nodiscard]] custom_element_registry & registry_of(value receiver);`

The registry a `customElements`-shaped receiver is - the page's for
anything that is not one of them.

<a id="contract-2"></a>

`[[nodiscard]] value create_html_element(context & cx, const std::string & name,`

`document.createElement(name, options)`: a defined name is constructed
through the author's class, anything else is a plain node wrapped;
`options.is` names a customized built-in's definition.

<a id="contract-3"></a>

`[[nodiscard]] std::size_t custom_definition_for(const read_txn & txn, node_id id) const;`

The definition this element's (local name, `is`) pair belongs to, or
npos.

<a id="contract-4"></a>

`[[nodiscard]] std::size_t custom_definition_of(context & cx, value receiver);`

The definition an HTML element constructor is running for: for a
receiver that is an element already, the definition whose construction
stack holds it (an upgrade); otherwise the definition whose prototype is
EXACTLY the receiver's, which is what `new C()` made it. npos when no
definition - the "Illegal constructor" every such call is.

<a id="contract-5"></a>

`[[nodiscard]] value construct_html_element(context & cx, value receiver,`

The HTML element constructor, HTML 4.13.4, for the receiver `super()` or
`new` handed a native: what HTMLElement and every HTML*Element interface
object share. `interface_name` is the interface whose constructor was
called, checked against the definition's local name.

<a id="contract-6"></a>

`[[nodiscard]] static std::string_view interface_name_for_tag(std::string_view tag);`

The interface an HTML tag is, by name - "HTMLUnknownElement" for a tag no
interface claims (bindings/element/interfaces.cpp owns the table).

<a id="contract-7"></a>

`void walk_custom_elements(const read_txn & txn, node_id start, bool connected, bool upgrade,`

One subtree in shadow-including tree order: upgrade what is new, diff
what is tracked. `upgrade` is false for the pass over DETACHED elements:
a candidate that is not in a document is not upgraded (HTML 4.13.5
upgrades on insertion and on `customElements.upgrade`), but one that
was already upgraded still gets its attributeChanged and disconnected
reactions. `roots_seen` collects the shadow roots the walk crossed.

<a id="contract-8"></a>

`void flush_custom_element_reactions(std::size_t from);`

Run the reactions enqueued at index `from` and after - one [CEReactions]
native's element queue - see the definition.

<a id="contract-9"></a>

`void run_upgrade(context & cx, std::size_t definition, node_id target, value wrapper);`

Run one upgrade reaction: HTML 4.13.5 "upgrade an element", with the
constructor fenced so an exception is reported and the element fails.

<a id="contract-10"></a>

`void report_custom_element_exception(context & cx, value thrown, std::string_view where);`

"Report the exception" for a callback or constructor that threw: the
window's error event, with the value.

<a id="contract-11"></a>

`[[nodiscard]] value construct_fenced(context & cx, value constructor, bool & threw,`

`new C()` inside a JavaScript try/catch, so a throw from the author's
constructor comes back as a value rather than unwinding through the
native that asked. Compiled on first use, like the listener fence.

<a id="contract-12"></a>

`flat_map<std::uint64_t, bool> loose_watch_;`

THE DETACHED ELEMENTS A SCAN MUST STILL WALK - see watch_loose - and
the generation the current scan stamps on what it reaches.

<a id="contract-13"></a>

`std::vector<std::pair<dom_bindings *, std::size_t>> adoptees_;`

Documents that received an adopted reaction from this scan, with the
floor their queue had - flushed once this scan's own reactions ran.

<a id="contract-14"></a>

`custom_element_registry * registry_ = nullptr;`

THIS DOCUMENT'S GLOBAL REGISTRY - null for a document a page made,
which has no browsing context and looks nothing up.

<a id="contract-15"></a>

`std::vector<std::unique_ptr<custom_element_registry>> registries_;`

Every registry of the realm, on the primary, and the map from a
registry object to its record - how a method on the shared prototype
learns which registry it was called on.

<a id="contract-16"></a>

`script::native_object * custom_elements_interface_ = nullptr;`

The CustomElementRegistry interface object, whose `retained` list roots
every constructor, prototype, callback and pending promise above - the
arrangement install_mutation_observer uses, for the same reason.

<a id="contract-17"></a>

`void install_element_internals(context & cx, script::object_object & html_element_proto);`

END custom elements
BEGIN element internals (bindings/element_internals.cpp)
`HTMLElement.prototype.attachInternals` and the ElementInternals it
answers with (HTML 4.13.7): the shadow root, the form-associated
members, the ARIA mixin, the CustomStateSet. Installed by
install_custom_elements, which owns HTMLElement.prototype.

<a id="contract-18"></a>

`std::function<value(node_id)> face_submission_value_;`

A form-associated custom element's submission value (undefined for an
element that is not one) - what "construct the entry list" appends.

<a id="contract-19"></a>

`[[nodiscard]] node_id form_owner_of(const read_txn & txn, node_id id) const;`

HTML 4.10.17.3, the form owner of a form-associated element: the form
its `form` attribute names in the same tree, else the nearest <form>
ancestor.

<a id="contract-20"></a>

`[[nodiscard]] bool form_control_disabled(const read_txn & txn, node_id id) const;`

HTML 4.10.18.5: a `disabled` attribute, or a disabled <fieldset>
ancestor the element is not inside the first <legend> of.

<a id="contract-21"></a>

`using css_declaration = style::css::declaration;`

THE CSSOM'S OWN COPY OF THE AUTHOR'S SHEETS, and why it is a copy.

`style::engine::add_sheet` FLATTENS a stylesheet into (selector,
declaration) rules and keeps no `css::stylesheet` at all, so the cascade
cannot be asked what rules a sheet has. The front end
(`style/css/parser.hpp`) is public and header-only, though, so the CSSOM
parses the document's own `<style>` and `<link rel=stylesheet>` text a
second time - with the SAME rules browser::load_author_styles uses, so
the two cannot disagree about which sheets exist.

The parse result is converted to OWNED strings immediately and the
`css::stylesheet` is dropped. That is deliberate: a `stylesheet` owns a
`pool` that every string_view in it points into, so holding one means
holding a container that never moves and never reallocates - and holding
it buys nothing here, because everything the CSSOM answers with is a
SERIALISATION rather than a slice of the source. See the file for what
"serialisation" means and why it is not the author's bytes.
The declaration itself, and the block algorithms over it, are
style/css/properties.hpp's: `el.style` keeps the same list, and the
shorthand expansion both need lives once.
