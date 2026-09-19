# Shell Bindings Mutation contracts

<a id="contract-1"></a>

`[[nodiscard]] script::object_object * mutation_observer_at(std::size_t index);`

The MutationObserver instance at that index, and its pending record
queue. The queue is a JavaScript ARRAY held on the instance rather than a
std::vector<value> here, so that rooting the instance roots the records
and there is one thing to keep alive instead of two.

<a id="contract-2"></a>

`[[nodiscard]] value make_mutation_record(context & cx, std::string_view type, node_id target);`

A MutationRecord with every field the IDL names, absent ones null and the
two node lists real empty arrays.

<a id="contract-3"></a>

`void queue_mutation_delivery();`

The microtask side. `queue_microtask` fixes its arguments at QUEUE time
and the record list has to stay open until the microtask RUNS, so what is
queued is a native trampoline that calls the second of these.

<a id="contract-4"></a>

`void take_mutation_snapshot();`

Re-read every observed node. Called by observe(), by disconnect() and at
the end of every record_mutations().

<a id="contract-5"></a>

`void sync_mutation_roots();`

WHAT KEEPS ALL THIS ALIVE: the `MutationObserver` interface object's
`retained` list - see script::native_object::retained. Refilled whenever
the set changes, which is rare and tiny.

<a id="contract-6"></a>

`bool mutation_delivery_queued_ = false;`

Whether a delivery microtask is already queued. Cleared when it runs,
which is what makes several mutations in one script turn arrive as ONE
callback holding several records.

<a id="contract-7"></a>

`bool delivering_mutations_ = false;`

Set while the mutation observer microtask runs (callbacks and the
slotchange events after them): a dispatch inside it must not drain
the microtask queue, which HTML's "performing a microtask checkpoint"
flag forbids - a queueMicrotask() queued before the delivery would
otherwise run between two slotchange events.

<a id="contract-8"></a>

`public:`

BEGIN web animations (bindings/animations.cpp)

THE SLICE OF WEB ANIMATIONS `css/css-values` OBSERVES: `element.animate`
makes an Animation over a KeyframeEffect, the page seeks it - `pause()`
then `currentTime = t` is what interpolation-testcommon.js does - and
reads the animated property back through getComputedStyle. So the model
is the specification's timing model over ONE clock (`now_ms_`, which is
also `document.timeline.currentTime`) and the effect value is an OVERLAY
on the cascade's text for that element: computed_style_entries asks
`animated_values` before it asks the style map, and every rule downstream
- a percentage against its containing block, an inset's used value - runs
on the interpolated text exactly as it would on a declared one.

Nothing RENDERS an animation: the overlay exists for getComputedStyle and
paint never sees it. Lengths, percentages, calc(), numbers and colours
interpolate; everything else is discrete.

CSS ANIMATIONS AND CSS TRANSITIONS ARE THE SAME MODEL DRIVEN FROM THE
CASCADE (bindings/animations/css.cpp): after every style resolution the
browser hands over the previous and the new style maps, and
`update_css_animations` makes a CSSAnimation for every `animation-name`
that names a `@keyframes` rule (CSS Animations 1 §5, updated in place
while the name stays at its index) and a CSSTransition for every
property whose before-change and after-change values differ and match
`transition-property` (CSS Transitions 1 §3, reversing included). Both
are `animation_record`s beside the script-made ones, sampled by the
same overlay in composite order: transitions, then animations, then
script (Web Animations 1 §5.4.2). Their events fire from `tick_animations`.

<a id="contract-9"></a>

`void update_css_animations(const read_txn & txn, const style::style_map & before,`

The cascade changed: start, update and cancel the CSS-owned animations
of every element in `txn`. `before` is the previous resolution's map
(the before-change style) and `after` the new one; a running animation's
current value is what the before-change style carries for its property.

<a id="contract-10"></a>

`void tick_animations();`

Fire the animation and transition events due since the last call, at the
clock's current time (CSS Animations 2 §4.2, CSS Transitions 2 §5).

<a id="contract-11"></a>

`[[nodiscard]] double next_animation_event_ms() const noexcept;`

How long until an animation next crosses a phase or iteration boundary
and has an event to fire; infinity when none will. The browser's wakeup.

<a id="contract-12"></a>

`[[nodiscard]] std::vector<std::pair<std::string, std::string>> animated_values(`

The animated properties of one element as (css name, text) pairs, at the
element's animations' CURRENT time. `underlying` answers the cascade's
text for a property, which is the missing endpoint of a one-keyframe
effect; `font_size` is the basis an `em` in a keyframe resolves against.

<a id="contract-13"></a>

`[[nodiscard]] std::uint64_t animation_stamp() const noexcept;`

Changes whenever ANY animated answer might: an animation was made,
seeked, paused or cancelled, or the clock moved while one is live. Zero
while no animation exists, so a page without animations never re-derives
a cached computed style on its account.
