// HTML 4.10's forms against html/semantics/forms: the select and option
// model, form.elements and its named access, the labels, constraint
// validation, the input types' numbers, the selection API, and FormData.
// One expression against a fresh page, and whatever it logged - the
// pattern of unit/dom_nodes_wpt.cpp.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><body>
<form id=f name=fm>
<label id=l1 for=t>T</label><input id=t name=t value=hello>
<label id=l2><span>S</span><select id=s name=s><option value=a>A</option><option value=b selected>B</option><optgroup><option value=c>C</option></optgroup></select></label>
<input type=checkbox name=cb value=yes><input type=radio name=r value=1><input type=radio name=r value=2 checked>
<textarea id=ta name=ta>text</textarea>
<input type=number id=n name=n min=2 max=10 step=2 value=4>
<input type=email id=e required>
<button id=btn name=btn value=go>Go</button>
<output id=o>out</output>
<fieldset id=fs><legend><input id=inlegend></legend><input id=infs></fieldset>
</form>
<input id=outside form=f name=outside value=z><input type=hidden id=hid value=q>
</body></html>)";

void is(const std::string & expression, const std::string & expected) {
    ctbrowser_test::is_in(page_html, expression, expected);
}

// --- the select and option model ------------------------------------------------

void test_select_options_and_selectedness() {
    // selected-index.html, select-value.html, common-HTMLOptionsCollection.html.
    is("(function () { var s = document.getElementById('s'); var o = [s.options.length,"
       " s.selectedIndex, s.options[1].selected, s.options[0].selected, s.value, s.type,"
       " s.options instanceof HTMLOptionsCollection, s.options === s.options, s.length,"
       " s.options.selectedIndex, s.item(2).value, s.namedItem('x'), s.options[2].index];"
       " return o.join(); })()",
       "3,1,true,false,b,select-one,true,true,3,1,c,,2");
    // Setting one option selected unselects the rest of a single select and
    // the store's value follows; selectedIndex = -1 clears; setting through
    // the shadowing `value` setter is read back.
    is("(function () { var s = document.getElementById('s'); s.options[2].selected = true;"
       " var o = [s.selectedIndex, s.options[1].selected, s.value];"
       " s.selectedIndex = -1; o.push(s.selectedIndex, s.value === '');"
       " s.selectedIndex = 0; o.push(s.value, s.selectedOptions.length, "
       "s.selectedOptions[0].value);"
       " s.value = 'b'; o.push(s.selectedIndex, s.options[1].selected);"
       " return o.join(); })()",
       "2,false,c,-1,true,a,1,a,1,true");
    // reset-form.html's precondition: the FIRST read after `value =` is one
    // option's selectedness, with nothing else having reconciled the store.
    is("(function () { var s = document.getElementById('s'); s.value = 'c';"
       " return [s.options[2].selected, s.options[1].selected].join(); })()",
       "true,false");
    // add/remove, on the select and on its options collection; length as a
    // setter. An option added selected is the one selected - every other
    // option's selectedness goes false (HTML 4.10.7) - and removing it leaves
    // none, so the first enabled option is selected.
    is("(function () { var s = document.getElementById('s');"
       " var made = new Option('D', 'd'); s.add(made); var o = [s.length, s.options[3].text,"
       " made.value, made instanceof HTMLOptionElement];"
       " s.options.add(new Option('Z', 'z', true, true), 0); o.push(s.selectedIndex, s.value);"
       " s.remove(0); o.push(s.length, s.selectedIndex);"
       " s.options.remove(9); s.options.length = 2; o.push(s.length);"
       " s.length = 4; o.push(s.length, s.options[3].value === '');"
       " try { s.add(document.createElement('div')); } catch (e) { o.push(e.name); }"
       " try { s.add(new Option('q'), document.body); } catch (e) { o.push(e.name); }"
       " return o.join(); })()",
       "4,D,d,true,0,z,4,0,2,4,true,TypeError,NotFoundError");
    // `new Option()` from nothing, and an option's text and form.
    is("(function () { var o = new Option(); var s = document.getElementById('s');"
       " s.options[0].text = ' A  B '; return [o.text, o.value, o.selected, o.index,"
       " s.options[0].text, s.options[0].form === document.getElementById('f'),"
       " document.querySelector('optgroup option').index].join(); })()",
       ",,false,0,A B,true,2");
}

// --- form.elements, length, named access, the owner ------------------------------

void test_form_elements_and_named_access() {
    // form-elements-nameditem-01.html, form-length.html, form_attribute.html.
    is("(function () { var f = document.getElementById('f'); var e = f.elements;"
       " return [e.length, f.length, e instanceof HTMLFormControlsCollection,"
       " e instanceof HTMLCollection, e[0].id, e.namedItem('t').id, e.t.id, e.r instanceof "
       "RadioNodeList,"
       " e.r.length, e.r.value, e.namedItem('nope'), e.outside.value, e[e.length - 1].id,"
       " document.getElementById('outside').form === f, e.fs.id].join(); })()",
       "14,14,true,true,t,t,t,true,2,2,,z,outside,true,fs");
    // RadioNodeList.value sets the checked radio; a fieldset's elements.
    is("(function () { var f = document.getElementById('f'); f.elements.r.value = '1';"
       " var r = document.querySelectorAll('input[name=r]');"
       " return [r[0].checked, r[1].checked, f.elements.r.value,"
       " document.getElementById('fs').elements.length, "
       "document.getElementById('fs').type].join(); })()",
       "true,false,1,2,fieldset");
}

// --- labels ------------------------------------------------------------------------

void test_labels_and_control() {
    is("(function () { var l1 = document.getElementById('l1'), l2 = document.getElementById('l2');"
       " var t = document.getElementById('t'), s = document.getElementById('s');"
       " return [l1.control === t, l2.control === s, l1.form === document.getElementById('f'),"
       " t.labels.length, t.labels[0] === l1, s.labels.length, s.labels[0] === l2,"
       " document.getElementById('btn').labels.length, t.labels instanceof NodeList].join(); })()",
       "true,true,true,1,true,1,true,0,true");
}

// --- constraint validation ----------------------------------------------------------

void test_validity() {
    // form-validation-validity-*.html and willValidate.html.
    is("(function () { var e = document.getElementById('e'), n = document.getElementById('n');"
       " var o = [e.willValidate, e.validity.valueMissing, e.validity.valid, e.checkValidity(),"
       " n.validity.valid, n.validity.stepMismatch, e.validity instanceof ValidityState];"
       " n.value = '5'; o.push(n.validity.stepMismatch, n.validity.valid);"
       " n.value = '12'; o.push(n.validity.rangeOverflow); n.value = '1'; "
       "o.push(n.validity.rangeUnderflow);"
       " n.value = 'abc'; o.push(n.validity.badInput); /* sanitised to '' - not bad input */"
       " e.value = 'nope'; o.push(e.validity.typeMismatch, e.validity.valueMissing);"
       " e.value = 'a@b.c'; o.push(e.validity.valid, e.validationMessage === '');"
       " e.setCustomValidity('bad'); o.push(e.validity.customError, e.validationMessage, "
       "e.validity.valid);"
       " e.setCustomValidity(''); o.push(e.validity.valid);"
       " return o.join(); })()",
       "true,true,false,false,true,false,true,true,false,true,true,false,true,false,true,true,true,"
       "bad,false,true");
    // The barred ones: disabled, readonly, a disabled fieldset's descendants
    // but not its first legend's, hidden, and the never-candidates.
    is("(function () { var t = document.getElementById('t'), fs = document.getElementById('fs');"
       " var o = [t.willValidate]; t.disabled = true; o.push(t.willValidate); t.disabled = false;"
       " t.readOnly = true; o.push(t.willValidate); t.readOnly = false;"
       " fs.disabled = true; o.push(document.getElementById('infs').willValidate,"
       " document.getElementById('inlegend').willValidate); fs.disabled = false;"
       " o.push(fs.willValidate, document.getElementById('o').willValidate,"
       " document.getElementById('btn').willValidate);"
       " var h = document.createElement('input'); h.type = 'hidden'; o.push(h.willValidate);"
       " var e = document.getElementById('e'); e.disabled = true; e.setCustomValidity('c');"
       " o.push(e.validity.valueMissing, e.validity.customError, e.validationMessage === '');"
       " e.disabled = false; e.setCustomValidity(''); o.push(e.validity.valueMissing);"
       " var r = document.createElement('input'); r.type = 'radio'; r.required = true;"
       " o.push(r.validity.valueMissing); r.name = 'g'; o.push(r.validity.valueMissing);"
       " return o.join(); })()",
       "true,false,false,false,true,false,false,true,false,false,true,true,true,false,true");
    // The `invalid` event, checkValidity on the form, and the pattern.
    is("(function () { var f = document.getElementById('f'), e = document.getElementById('e');"
       " var fired = []; e.addEventListener('invalid', function (ev) { fired.push(ev.type, "
       "ev.bubbles, ev.cancelable); });"
       " var o = [f.checkValidity(), fired.join('/'), f.reportValidity()];"
       " e.value = 'x@y.z'; o.push(f.checkValidity());"
       " var p = document.createElement('input'); p.pattern = '[a-z]+'; p.value = 'abc';"
       " o.push(p.validity.patternMismatch); p.value = 'ABC'; o.push(p.validity.patternMismatch);"
       " p.pattern = '('; o.push(p.validity.patternMismatch);"
       " return o.join(); })()",
       "false,invalid/false/true,false,true,false,true,false");
    // A required radio group and a required select with a placeholder.
    is("(function () { var r = document.querySelectorAll('input[name=r]'); r[0].required = true;"
       " var o = [r[0].validity.valueMissing]; r[1].checked = false; "
       "o.push(r[0].validity.valueMissing);"
       " var s = document.createElement('select'); s.required = true; s.innerHTML = '<option "
       "value=\"\">--</option><option>x</option>';"
       " o.push(s.validity.valueMissing); s.selectedIndex = 1; o.push(s.validity.valueMissing);"
       " var c = document.querySelector('input[name=cb]'); c.required = true; "
       "o.push(c.validity.valueMissing);"
       " c.checked = true; o.push(c.validity.valueMissing);"
       " return o.join(); })()",
       "false,true,true,false,true,false");
}

// --- valueAsNumber, valueAsDate, stepUp/stepDown ---------------------------------

void test_input_numbers_and_dates() {
    is("(function () { var n = document.getElementById('n'); var o = [n.valueAsNumber];"
       " n.stepUp(); o.push(n.value); n.stepDown(2); o.push(n.value); n.value = '5'; n.stepUp(); "
       "o.push(n.value);"
       " n.valueAsNumber = 8; o.push(n.value); n.valueAsNumber = NaN; o.push(n.value === '');"
       " try { n.valueAsNumber = Infinity; } catch (e) { o.push(e.name); }"
       " var t = document.getElementById('t'); o.push(isNaN(t.valueAsNumber));"
       " try { t.stepUp(); } catch (e) { o.push(e.name); }"
       " return o.join(); })()",
       "4,6,2,6,8,true,TypeError,true,InvalidStateError");
    // hidden.html: a hidden input's value is its attribute, it has no files
    // and no list; `indeterminate` is plain state.
    is("(function () { var h = document.getElementById('hid'); var o = [h.value, h.files, h.list,"
       " h.willValidate, h.indeterminate]; h.value = 'w'; h.indeterminate = true;"
       " o.push(h.value, h.getAttribute('value'), h.indeterminate); return o.join(); })()",
       "q,,,false,false,w,w,true");
    // A 30-digit year is an invalid value, not signed-overflow UB (INT-004).
    is("(function () { var d = document.createElement('input'); d.type = 'date'; d.value = "
       "'"
       "123456789012345678901234567890-01-01'; return d.value === '' && isNaN(d.valueAsNumber); "
       "})()",
       "true");
    is("(function () { var d = document.createElement('input'); d.type = 'date'; d.value = "
       "'2020-02-29';"
       " var o = [d.valueAsNumber, d.valueAsDate.getUTCFullYear(), d.valueAsDate.getUTCDate()];"
       " d.valueAsDate = new Date(Date.UTC(1999, 11, 31)); o.push(d.value);"
       " d.valueAsNumber = 0; o.push(d.value); d.stepUp(); o.push(d.value);"
       " d.value = '2021-02-29'; o.push(d.value === '' || isNaN(d.valueAsNumber));"
       " var m = document.createElement('input'); m.type = 'month'; m.value = '1970-03';"
       " o.push(m.valueAsNumber, m.valueAsDate.getUTCMonth()); m.stepDown(); o.push(m.value);"
       " var w = document.createElement('input'); w.type = 'week'; w.value = '2021-W01';"
       " o.push(new Date(w.valueAsNumber).getUTCDate(), w.valueAsDate instanceof Date);"
       " var tm = document.createElement('input'); tm.type = 'time'; tm.value = '13:05:09.5';"
       " o.push(tm.valueAsNumber); tm.stepUp(); o.push(tm.value);"
       " var dl = document.createElement('input'); dl.type = 'datetime-local'; dl.value = "
       "'2020-01-02T03:04';"
       " o.push(dl.valueAsNumber, dl.valueAsDate);"
       " try { dl.valueAsDate = null; } catch (e) { o.push(e.name); }"
       " return o.join(); })()",
       "1582934400000,2020,29,1999-12-31,1970-01-01,1970-01-02,true,2,2,1970-02,4,true,"
       "47109500,13:06,1577934240000,,InvalidStateError");
}

// --- the selection API ---------------------------------------------------------------

void test_selection_api() {
    // selection-start-end.html, textfieldselection-setRangeText.html.
    is("(function () { var t = document.getElementById('t'); t.setSelectionRange(1, 3);"
       " var o = [t.selectionStart, t.selectionEnd, t.selectionDirection];"
       " t.setSelectionRange(4, 2, 'backward'); o.push(t.selectionStart, t.selectionEnd, "
       "t.selectionDirection);"
       " t.selectionStart = 9; o.push(t.selectionStart, t.selectionEnd); t.select(); "
       "o.push(t.selectionStart, t.selectionEnd);"
       " t.setRangeText('XY', 1, 3, 'select'); o.push(t.value, t.selectionStart, t.selectionEnd);"
       " t.setRangeText('', 0, 1, 'end'); o.push(t.value, t.selectionStart);"
       " var e = document.getElementById('e'); o.push(e.selectionStart);"
       " try { e.setSelectionRange(0, 1); } catch (x) { o.push(x.name); }"
       " var ta = document.getElementById('ta'); ta.value = 'a\\u{1F600}b'; "
       "ta.setSelectionRange(1, 3);"
       " o.push(ta.selectionStart, ta.selectionEnd, ta.textLength);"
       " return o.join(); })()",
       "1,3,none,2,2,backward,5,5,0,5,hXYlo,1,3,XYlo,0,,InvalidStateError,1,3,4");
}

// --- FormData, submit, requestSubmit, reset -------------------------------------------

void test_form_data_and_submission() {
    is("(function () { var f = document.getElementById('f'); var fd = new FormData(f);"
       " var o = [fd.get('t'), fd.get('s'), fd.get('r'), fd.has('cb'), fd.get('btn'), "
       "fd.get('outside'),"
       " fd.getAll('ta').length, fd.get('n')];"
       " fd.append('x', 1); fd.set('t', 'bye'); fd.delete('r'); o.push(fd.get('x'), fd.get('t'), "
       "fd.has('r'));"
       " var keys = []; for (var k of fd.keys()) { keys.push(k); } o.push(keys.join('+'));"
       " var pairs = []; fd.forEach(function (v, k) { pairs.push(k + '=' + v); }); "
       "o.push(pairs.length);"
       " o.push(Object.prototype.toString.call(fd), fd instanceof FormData);"
       " try { new FormData({}); } catch (e) { o.push(e.name); }"
       " var fd2 = new FormData(f, document.getElementById('btn')); o.push(fd2.get('btn'));"
       " return o.join(); })()",
       "hello,b,2,false,,z,1,4,1,bye,false,t+s+ta+n+outside+x,6,[object "
       "FormData],true,TypeError,go");
    is("(function () { var f = document.getElementById('f'); var seen = [];"
       " f.addEventListener('submit', function (e) { seen.push('submit:' + (e.submitter && "
       "e.submitter.id)); e.preventDefault(); });"
       " f.addEventListener('formdata', function (e) { seen.push('formdata:' + "
       "e.formData.get('t')); });"
       " f.requestSubmit(); seen.push('|'); document.getElementById('e').value = 'a@b.c';"
       " f.requestSubmit(document.getElementById('btn')); seen.push('|'); f.submit();"
       " try { f.requestSubmit(document.getElementById('t')); } catch (x) { seen.push(x.name); }"
       " try { f.requestSubmit(document.createElement('button')); } catch (x) { seen.push(x.name); "
       "}"
       " return seen.join(); })()",
       "|,submit:btn,|,formdata:hello,TypeError,NotFoundError");
    is("(function () { var f = document.getElementById('f'); var t = document.getElementById('t');"
       " var s = document.getElementById('s'); t.value = 'changed'; s.selectedIndex = 0;"
       " var fired = 0; f.addEventListener('reset', function () { fired++; });"
       " f.reset(); return [fired, t.value, s.selectedIndex, s.value].join(); })()",
       "1,hello,1,b");
}

// A BUTTON'S ACTIVATION BEHAVIOUR submits and resets through the same algorithm
// requestSubmit() and reset() run: form-submission-algorithm.html's SubmitEvent
// carrying the button, input-type-change-submit.html's text field made a
// submit button by its own click listener, button-click-submits-with-
// commandfor.html's plain button, and reset.html's reset button that names
// its form with `form=` from outside it.
void test_activation_submits_and_resets() {
    is("(function () { var f = document.getElementById('f'); var seen = [];"
       " document.getElementById('e').value = 'a@b.c';"
       " f.addEventListener('submit', function (e) {"
       " seen.push((e instanceof SubmitEvent) + ':' + (e.submitter && e.submitter.id));"
       " e.preventDefault(); });"
       " document.getElementById('btn').click();"
       " var t = document.getElementById('t');"
       " t.addEventListener('click', function () { t.type = 'submit'; }); t.click();"
       " var b = document.createElement('button'); b.setAttribute('commandfor', 'x');"
       " f.appendChild(b); b.click();"
       " var r = document.createElement('input'); r.type = 'RESET'; r.setAttribute('form', 'f');"
       " document.body.appendChild(r); var o = document.getElementById('outside');"
       " o.value = 'changed'; r.click(); seen.push(o.value);"
       " return seen.join(); })()",
       "true:btn,true:t,z");
    // clicking-interactive-content.html: a <textarea> has no activation
    // behaviour, so its click's activation target is the <label> around it,
    // whose click at the textarea came back to the label - for ever.
    is("(function () { var l = document.createElement('label');"
       " var t = l.appendChild(document.createElement('textarea'));"
       " document.body.appendChild(l); var n = 0;"
       " t.addEventListener('click', function () { n++; }); t.click(); return n > 0; })()",
       "true");
}

// --- output and textarea ---------------------------------------------------------------

void test_output_and_textarea() {
    is("(function () { var o = document.getElementById('o'); var out = [o.value, o.defaultValue, "
       "o.type];"
       " o.value = 'v'; out.push(o.value, o.defaultValue, o.textContent); o.defaultValue = 'd';"
       " out.push(o.value, o.defaultValue); var ta = document.getElementById('ta');"
       " out.push(ta.type, ta.textLength, ta.defaultValue); ta.defaultValue = 'zz'; "
       "out.push(ta.textContent);"
       " return out.join(); })()",
       "out,out,output,v,out,v,v,d,textarea,4,text,zz");
    // progress.window.js and meter.html: the clamped current values.
    is("(function () { var p = document.createElement('progress'); var o = [p.value, p.position];"
       " p.value = 2; o.push(p.value, p.position); p.max = 4; o.push(p.value, p.position);"
       " var m = document.createElement('meter'); m.value = 5; o.push(m.value, m.max, m.min, "
       "m.optimum);"
       " m.min = 2; m.max = 10; m.low = 1; m.high = 20; o.push(m.value, m.low, m.high, m.optimum);"
       " return o.join(); })()",
       "0,-1,1,1,2,0.5,1,1,0,0.5,5,2,10,6");
}

} // namespace

// --- the form owner, HTML 4.10.17.3 ----------------------------------------------------

void test_form_owner_attribute() {
    // form_attribute.html: `form=""` names nothing; a DISCONNECTED control's
    // `form` attribute names nothing either - its owner is its nearest form
    // ancestor (4.10.18.3 step 4 is for a connected one), so a non-form with
    // the id earlier in a detached tree changes nothing; connected, the
    // non-form with the id earlier wins nothing.
    is("(function () { var f = document.getElementById('f'); var b = "
       "document.createElement('button');"
       " f.appendChild(b); var o = [b.form === f]; b.setAttribute('form', ''); o.push(b.form);"
       " b.setAttribute('form', 'f'); o.push(b.form === f);"
       " var d = document.createElement('div'); var f2 = document.createElement('form');"
       " f2.id = 'f2'; d.appendChild(f2); var c = document.createElement('input');"
       " c.setAttribute('form', 'f2'); f2.appendChild(c); o.push(c.form === f2);"
       " var s = document.createElement('span'); s.id = 'f2'; d.insertBefore(s, f2);"
       " o.push(c.form === f2); document.body.appendChild(d);"
       " o.push(c.form); return o.join(); })()",
       "true,,true,true,true,");
}

// --- the selection across a value change, HTML 4.10.5.3 ------------------------------

void test_selection_survives_the_same_value() {
    // selection-after-content-change.html: the same value leaves the
    // selection alone; a different one moves the caret to the end and resets
    // the direction.
    is("(function () { var i = document.createElement('input'); i.value = 'hello';"
       " i.setSelectionRange(1, 3, 'backward'); i.value = 'hello';"
       " var o = [i.selectionStart, i.selectionEnd, i.selectionDirection];"
       " i.value = 'hello!'; o.push(i.selectionStart, i.selectionEnd, i.selectionDirection);"
       " return o.join(); })()",
       "1,3,backward,6,6,none");
}

// --- the list of options, HTML 4.10.7 -------------------------------------------------

void test_list_of_options_nesting() {
    // select-selectedOptions-nesting.window.js: an option inside another
    // option, an hr, a nested select or a nested optgroup is not in the list.
    is("(function () { var o = []; for (var parent of ['option', 'hr', 'select', 'optgroup']) {"
       " var s = document.createElement('select'); s.innerHTML = '<optgroup><option>1';"
       " var p = s.firstChild.appendChild(document.createElement(parent));"
       " var x = p.appendChild(document.createElement('option')); x.setAttribute('selected', '');"
       " x.textContent = '2';"
       " o.push(s.selectedOptions.length + ':' + s.value + ':' + s.options.length); }"
       " var d = document.createElement('select'); d.innerHTML = '<div><optgroup><div><option>1';"
       " o.push(d.value + ':' + d.options.length); return o.join(); })()",
       "1:1:2,1:1:1,1:1:1,1:1:1,1:1");
}

// --- the autocomplete IDL attribute, HTML 4.10.18.7.1 --------------------------------

void test_autocomplete_tokens() {
    // form-autocomplete.html.
    is("(function () { var i = document.createElement('input'); var o = [i.autocomplete];"
       " i.setAttribute('autocomplete', ' ON\\t'); o.push(i.autocomplete);"
       " i.setAttribute('autocomplete', 'foo off'); o.push(i.autocomplete === '');"
       " i.type = 'hidden'; i.setAttribute('autocomplete', 'off'); o.push(i.autocomplete === '');"
       " var t = document.createElement('textarea');"
       " t.setAttribute('autocomplete', ' HOME\\ntel'); o.push(t.autocomplete);"
       " t.setAttribute('autocomplete', '  section-FOO  billing work email webauthn');"
       " o.push(t.autocomplete);"
       " t.setAttribute('autocomplete', 'foo section-foo billing name'); o.push(t.autocomplete === "
       "'');"
       " t.setAttribute('autocomplete', 'call-sign'); o.push(t.autocomplete === '');"
       " t.autocomplete = 'given-name'; o.push(t.getAttribute('autocomplete'));"
       " return o.join('|'); })()",
       "|on|true|true|home tel|section-foo billing work email webauthn|true|true|given-name");
}

// --- the value sanitization algorithm and the value modes, HTML 4.10.5.1 ----------

void test_value_sanitization_and_type_change() {
    // type-change-state.html, valueMode.html, number.html, range.html, color.html.
    is("(function () { var i = document.createElement('input'); var o = [];"
       " i.value = '  foo\\rbar  '; o.push(JSON.stringify(i.value));"
       " i.type = 'url'; o.push(i.value); i.type = 'number'; o.push(i.value === '');"
       " i.value = '50'; i.type = 'range'; o.push(i.value); i.type = 'color'; o.push(i.value);"
       " i.value = '#ABCDEF'; o.push(i.value); i.type = 'submit'; o.push(i.value, "
       "i.getAttribute('value'));"
       " i.type = 'text'; o.push(i.value); i.value = 'typed'; i.type = 'checkbox'; "
       "o.push(i.getAttribute('value'));"
       " i.removeAttribute('value'); o.push(i.value); i.type = 'file'; o.push(i.value === '');"
       " try { i.value = 'x'; } catch (e) { o.push(e.name); }"
       " var n = document.createElement('input'); n.type = 'number'; n.value = 'abc'; "
       "o.push(n.value === '');"
       " n = document.createElement('input'); n.type = 'number';"
       " n.setAttribute('value', '1e3'); o.push(n.value); n.setAttribute('value', '1d+2'); "
       "o.push(n.value === '');"
       " var r = document.createElement('input'); r.type = 'range'; r.min = '10'; r.max = '20';"
       " r.value = '5'; o.push(r.value); r.value = 'junk'; o.push(r.value);"
       " var d = document.createElement('input'); d.type = 'datetime-local'; d.value = "
       "'2014-01-01 11:11:00'; o.push(d.value);"
       " var t = document.createElement('input'); t.value = null; o.push(t.value === '');"
       " var c = document.createElement('input'); c.value = 'foo bar'; var cc = c.cloneNode();"
       " cc.setAttribute('value', 'other'); o.push(cc.value);"
       " var cb = document.createElement('input'); cb.type = 'checkbox'; cb.checked = true;"
       " o.push(cb.cloneNode().checked);"
       " return o.join(); })()",
       "\"  foobar  \",foobar,true,50,#000000,#abcdef,#abcdef,#abcdef,#abcdef,typed,on,true,"
       "InvalidStateError,true,1e3,true,10,15,2014-01-01T11:11,true,foo bar,true");
}

// --- round nine: barred validity, groups, selection seeding, FACE ----------------------

void test_barred_validity_groups_and_selection() {
    // form-validation-validity-*.html: a barred control keeps its flags; only
    // a text-ish valueMissing asks for mutability. input-stepup.html: the
    // week state's default step base. cloning-steps.html, radio.html.
    is("(function () { var i = document.createElement('input'); i.pattern = '[a-z]+';"
       " i.value = 'ABC'; i.disabled = true;"
       " var o = [i.validity.patternMismatch, i.validity.valid, i.willValidate, "
       "i.checkValidity()];"
       " var r = document.createElement('input'); r.required = true; r.readOnly = true;"
       " o.push(r.validity.valueMissing);"
       " var c = document.createElement('input'); c.type = 'checkbox'; c.required = true;"
       " c.disabled = true; o.push(c.validity.valueMissing);"
       " var w = document.createElement('input'); w.type = 'week'; w.value = '1970-W01';"
       " o.push(w.validity.stepMismatch); w.stepUp(); o.push(w.value);"
       " var b = document.createElement('button'); b.type = 'reset'; o.push(b.willValidate);"
       " var k = document.createElement('input'); k.type = 'checkbox'; k.indeterminate = true;"
       " o.push(k.cloneNode().indeterminate);"
       " var d = document.createElement('div'); d.innerHTML = '<input type=radio name=g checked>"
       "<input type=radio name=g><input type=radio name=h checked>';"
       " var rs = d.querySelectorAll('input'); rs[1].checked = true;"
       " o.push(rs[0].checked, rs[1].checked, rs[2].checked);"
       " return o.join(); })()",
       "true,false,false,true,false,true,false,1970-W02,false,true,false,true,true");
    // defaultSelection.html, type-change-state.html, value-defaultValue-
    // textContent.html, email.html.
    is("(function () { var d = document.createElement('div');"
       " d.innerHTML = '<input value=foo><textarea>foo</textarea>';"
       " var i = d.firstChild, ta = d.lastChild; var o = [i.selectionStart, ta.selectionStart];"
       " i.value = 'foo'; o.push(i.selectionStart); i.value = 'Foo'; o.push(i.selectionEnd);"
       " i.type = 'email'; i.type = 'text'; o.push(i.selectionStart);"
       " ta.textContent = 'a\\r\\nb'; o.push(JSON.stringify(ta.value), ta.defaultValue.length);"
       " ta.appendChild(document.createElement('span')).textContent = 'zz';"
       " o.push(JSON.stringify(ta.value));"
       " var e = document.createElement('input'); e.type = 'email'; e.multiple = true;"
       " e.value = ' a@b.c , d@e.f '; o.push(e.value); e.multiple = false; o.push(e.value);"
       " var e2 = document.createElement('input'); e2.type = 'email';"
       " e2.value = 'x@exämle.com'; o.push(e2.value);"
       " var n = document.createElement('input'); n.type = 'number'; n.value = '.5';"
       " o.push(n.value);"
       " return o.join('|'); })()",
       "0|0|0|3|0|\"a\\nb\"|4|\"a\\nb\"|a@b.c,d@e.f|a@b.c,d@e.f|x@xn--exmle-hra.com|.5");
    // HTMLLegendElement.html, and a form-associated custom element as a
    // form control: listed, submitted, validated, reset, disabled.
    is("(function () { var f = document.createElement('form');"
       " f.innerHTML = '<fieldset><legend id=lg></legend></fieldset><legend id=l2></legend>';"
       " var o = [f.querySelector('#lg').form === f, f.querySelector('#l2').form === null];"
       " customElements.define('x-face', class extends HTMLElement {"
       " static get formAssociated() { return true; }"
       " constructor() { super(); this.i = this.attachInternals(); this.resets = 0; }"
       " formResetCallback() { this.resets++; } });"
       " var g = document.body.appendChild(document.createElement('form'));"
       " g.innerHTML = '<x-face name=q></x-face><input name=r>'; var x = g.firstChild;"
       " o.push(g.elements.length); x.i.setFormValue('v'); o.push(new FormData(g).get('q'));"
       " x.i.setValidity({valueMissing: true}, 'm'); o.push(g.checkValidity());"
       " g.reset(); o.push(x.resets);"
       " x.setAttribute('disabled', ''); o.push(new FormData(g).has('q'));"
       " return o.join(); })()",
       "true,true,2,v,false,1,false");
}

void test_files_and_data_transfer() {
    // the-input-element/files.html, form-data-set-empty-file.window.js,
    // newline-normalization.html's DataTransfer set-up.
    is("(function () { var i = document.createElement('input'); i.type = 'file';"
       " var f = i.files; var o = [f instanceof FileList, f.length, i.files === f];"
       " var dt = new DataTransfer();"
       " dt.items.add(new File(['ab'], 'x.txt', {type: 'text/plain'}));"
       " dt.items.add('hi', 'text/plain');"
       " o.push(dt.items.length, dt.files.length, dt.types.join('+'), dt.getData('text'));"
       " i.files = dt.files; o.push(i.files.length, i.files[0].name, i.files.item(0).size);"
       " try { i.files = []; } catch (e) { o.push(e.name); }"
       " var t = document.createElement('input'); o.push(t.files);"
       " var form = document.createElement('form');"
       " var e = form.appendChild(document.createElement('input')); e.type = 'file';"
       " e.name = 'up'; var v = new FormData(form).get('up');"
       " o.push(v instanceof File, v.name === '', v.type, v.size);"
       " return o.join(); })()",
       "true,0,true,2,1,text/plain+Files,hi,1,x.txt,2,TypeError,,true,true,"
       "application/octet-stream,0");
}

void test_color_events_options_and_entries() {
    // the-input-element/color.window.js: the colour space and alpha shape
    // the value, a clean control re-reading its attribute.
    is("(function () { var i = document.createElement('input'); i.type = 'color';"
       " i.setAttribute('value', ' #FFFFFF '); var o = [i.value]; i.value = 'blue';"
       " i.setAttribute('alpha', ''); o.push(i.value); i.value = 'white';"
       " i.colorSpace = 'display-p3'; o.push(i.value); return o.join('|'); })()",
       "#ffffff|color(srgb 0 0 1)|color(display-p3 1 1 1)");
    // SubmitEvent.window.js, FormDataEvent.window.js.
    is("(function () { var b = document.getElementById('btn');"
       " var e = new SubmitEvent('submit', {submitter: b, bubbles: true});"
       " var o = [e instanceof Event, e.submitter === b, e.bubbles,"
       " new SubmitEvent('x').submitter];"
       " try { new SubmitEvent('x', {submitter: 1}); o.push('no'); } catch (x) { o.push(x.name); }"
       " try { SubmitEvent('x'); o.push('no'); } catch (x) { o.push(x.name); }"
       " var fd = new FormData();"
       " o.push(new FormDataEvent('formdata', {formData: fd}).formData === fd);"
       " try { new FormDataEvent('x'); o.push('no'); } catch (x) { o.push(x.name); }"
       " return o.join(); })()",
       "true,true,true,,TypeError,TypeError,true,TypeError");
    // option-element-constructor.html; select.value with no matching option;
    // the HTMLOptionsCollection indexed setter; button.value is no
    // [LegacyNullToEmptyString]; accessKeyLabel.
    is("(function () { var a = new Option('t', 'v', true); var b = new Option('t', 'v', false, "
       "true);"
       " var o = [a.selected, a.defaultSelected, b.selected, b.defaultSelected];"
       " var s = document.createElement('select');"
       " s.innerHTML = '<option>one</option><option>two</option>'; o.push(s.value);"
       " s.value = 'none'; o.push(s.value, s.selectedIndex);"
       " var t = document.createElement('select'); var n = new Option('a', 'a');"
       " t.options[2] = n; o.push(t.options.length, t.options[2] === n);"
       " t.options[0] = null; o.push(t.options.length);"
       " var btn = document.createElement('button'); btn.value = null;"
       " o.push(btn.getAttribute('value'));"
       " var k = document.createElement('b'); k.accessKey = 's 0'; o.push(k.accessKeyLabel === '');"
       " k.accessKey = 'k'; o.push(k.accessKeyLabel);"
       " return o.join(); })()",
       "false,true,true,false,one,,-1,3,true,2,null,true,Alt+K");
    // The select model sees its list change: a removal with nothing left
    // selected resets to the first option, an option put back ahead is the
    // first again (select-validity.html, optgroup-removal.window.js); and
    // `value` sets the selectedness of a multiple select too.
    is("(function () { var s = document.createElement('select'); s.required = true;"
       " var p = s.appendChild(document.createElement('option'));"
       " var n = s.appendChild(document.createElement('option')); n.textContent = 'x';"
       " var o = [s.checkValidity()]; p.remove(); o.push(s.checkValidity());"
       " s.prepend(p); o.push(s.checkValidity());"
       " var g = document.createElement('select');"
       " g.innerHTML = '<optgroup><option>1</optgroup><optgroup><option>2'; o.push(g.value);"
       " g.querySelector('optgroup').remove(); o.push(g.value);"
       " var m = document.createElement('select'); m.multiple = true;"
       " m.innerHTML = '<option>a</option><option>b</option>'; m.value = 'b';"
       " o.push(m.value, m.selectedOptions.length);"
       " return o.join(); })()",
       "false,true,false,1,2,b,1");
    // DataTransfer.files is [SameObject] and live; a FormData entry's name
    // and string value are scalar value strings; a submission's pairs are
    // CR LF-normalised only on the way out.
    is("(function () { var dt = new DataTransfer(); var f = dt.files;"
       " dt.items.add(new File(['x'], 'n.txt'));"
       " var o = [dt.files === f, f.length, f[0].name];"
       " var fd = new FormData(); fd.append('a\\uDC01', 'b\\uD800');"
       " o.push(fd.has('a\\uFFFD'), fd.get('a\\uDC01') === 'b\\uFFFD');"
       " fd.append('c\\nd', 'e'); o.push(fd.has('c\\nd'));"
       " return o.join(); })()",
       "true,1,n.txt,true,true,true");
}

int main() {
    test_color_events_options_and_entries();
    test_barred_validity_groups_and_selection();
    test_files_and_data_transfer();
    test_select_options_and_selectedness();
    test_form_elements_and_named_access();
    test_labels_and_control();
    test_validity();
    test_input_numbers_and_dates();
    test_value_sanitization_and_type_change();
    test_autocomplete_tokens();
    test_list_of_options_nesting();
    test_selection_survives_the_same_value();
    test_form_owner_attribute();
    test_selection_api();
    test_form_data_and_submission();
    test_activation_submits_and_resets();
    test_output_and_textarea();
    REPORT("forms_wpt");
}
