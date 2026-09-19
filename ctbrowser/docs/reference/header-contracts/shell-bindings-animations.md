# Shell Bindings Animations contracts

<a id="contract-1"></a>

`[[nodiscard]] static bool transitionable(std::string_view property, std::string_view from,`

Whether a transition may run between two values (CSS Transitions §3:
numeric of one type, or two colours).

<a id="contract-2"></a>

`void cancel_record(std::size_t index);`

"Cancel an animation" (§4.4.12): idle, the finished promise rejected.
Its event, if it is a CSS-owned one, fires from the next tick.

<a id="contract-3"></a>

`void update_css_transitions(node_id element, std::size_t tree_order,`

The element's `animation-*` / `transition-*` lists, and the records it owns.
`records` are the element's own live CSS records (owned_), indexed once
per update rather than searched for per element.

<a id="contract-4"></a>

`void install_animations(context & cx);`

`Animation`, `KeyframeEffect`, `DocumentTimeline`, `document.timeline`,
`document.getAnimations`, and `animate`/`getAnimations` on
Element.prototype. Called from install_dom_interfaces, which is where
that prototype exists; a second call is a no-op.

<a id="contract-5"></a>

`[[nodiscard]] value make_keyframe_effect(context & cx, node_id target, value keyframes,`

`new KeyframeEffect(target, keyframes, options)` and `new Animation(effect)`,
shared with `element.animate`, which is the two of them plus `play()`.

<a id="contract-6"></a>

`[[nodiscard]] std::size_t push_effect(context & cx, keyframe_effect_record made);`

A record made by C++ - a transition's, a CSS animation's - given its
KeyframeEffect object and filed; returns its index in effects_.

<a id="contract-7"></a>

`[[nodiscard]] double animation_current_time(const animation_record & a) const noexcept;`

The timing model, Web Animations §4.4-4.5. `current_time` is NaN when
unresolved; `play_state` is one of idle/running/paused/finished.

<a id="contract-8"></a>

`void update_finished_state(context & cx, std::size_t index);`

Settle the finished promise when the animation is in the finished state,
and bump the stamp; every state change ends here.

<a id="contract-9"></a>

`[[nodiscard]] std::vector<std::size_t> animations_on(node_id id, bool subtree) const;`

The animations whose effect targets `id` - or any descendant of it with
`subtree` - that are not idle, in creation order.

<a id="contract-10"></a>

`public:`

END web animations
BEGIN custom elements (bindings/custom_elements.cpp)

<a id="contract-11"></a>

`void react_custom_elements();`

WHAT `mutated()` CALLS AFTER THE OBSERVERS. A definition covers every
element with its name wherever the parser or a script put one, and the
funnel does not say which node changed - so this walks the tree, upgrades
any element a definition now covers, diffs the tracked ones against what
they were (connected, parent, observed attributes) and RUNS the reactions
before returning, which is what [CEReactions] means. Returns on the first
line when nothing was ever defined.

<a id="contract-12"></a>

`void upgrade_created_subtree(node_id root);`

A SUBTREE A NATIVE JUST MADE - cloneNode, importNode, a fragment parse
into a detached element: DOM "create an element" enqueues an upgrade for
every candidate it makes whether or not the result is connected, and
the scan cannot see a detached subtree it has never been told about.

<a id="contract-13"></a>

`[[nodiscard]] value custom_elements_registry(context & cx);`

THIS DOCUMENT'S OWN `customElements`. A frame's document has a browsing
context and so a registry of its own (HTML 4.13.3) even though the realm
- HTMLElement, every prototype - is the page's: frames.cpp hangs this
on `contentWindow`, and asking for it marks the document as a frame's.
The primary's is the `customElements` global.
