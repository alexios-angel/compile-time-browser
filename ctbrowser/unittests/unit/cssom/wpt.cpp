// WHAT css/cssom ASKS THE OBJECT MODEL, one case per behaviour that moved.
//
// The third CSSOM file beside rules.cpp and sheets.cpp: every case
// here is a string or a boolean a `css/cssom` test compares, and each one names
// the file it stands in for. `logged` is shared through probe.hpp.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "probe.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

void test_computed_background_positions() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<div id="target" style="font-size: 20px"></div><script>
        const target = document.getElementById('target');
        for (const value of ['center right 7%', 'top 1em center',
                             'center, left, bottom', 'top 17px right -18px']) {
            target.style.backgroundPosition = value;
            console.log(value + '=' + getComputedStyle(target).backgroundPosition);
        }
        target.style.background = 'right 1em center, left bottom 16%';
        console.log('shorthand=' + getComputedStyle(target).backgroundPosition);
        for (const property of ['object-position', 'mask-position']) {
            target.style.setProperty(property, 'right -18px top 17px');
            console.log(property + '=' + getComputedStyle(target).getPropertyValue(property));
        }
    </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "center right 7%="), std::string{"center right 7%=93% 50%"});
    CHECK_EQ(logged(page, "top 1em center="), std::string{"top 1em center=50% 20px"});
    CHECK_EQ(logged(page, "center, left, bottom="),
             std::string{"center, left, bottom=50% 50%, 0% 50%, 50% 100%"});
    CHECK_EQ(logged(page, "top 17px right -18px="),
             std::string{"top 17px right -18px=calc(100% + 18px) 17px"});
    CHECK_EQ(logged(page, "shorthand="), std::string{"shorthand=calc(100% - 20px) 50%, 0% 84%"});
    CHECK_EQ(logged(page, "object-position="),
             std::string{"object-position=calc(100% + 18px) 17px"});
    CHECK_EQ(logged(page, "mask-position="), std::string{"mask-position=calc(100% + 18px) 17px"});
}

void test_background_cascade() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<style>
        #target { font-size: 40px; --bg: none center / calc(1em - 60px) 1em no-repeat text;
                  background: var(--bg); }
        #child { font-size: 10px; background: inherit; }
        </style><div id="target"><div id="child"></div></div><script>
        for (const id of ['target', 'child']) {
            const s = getComputedStyle(document.getElementById(id));
            console.log(id + '=' + s.backgroundSize + '|' + s.backgroundRepeat + '|' +
                        s.backgroundOrigin + '|' + s.backgroundClip);
        }
        const target = document.getElementById('target');
        target.style.background = 'text border-area';
        console.log('clip=' + getComputedStyle(target).backgroundClip);
        target.style.background = 'none';
        console.log('reset=' + getComputedStyle(target).backgroundRepeat + '|' +
                    getComputedStyle(target).backgroundClip);
    </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "target="), std::string{"target=0px 40px|no-repeat|border-box|text"});
    CHECK_EQ(logged(page, "child="), std::string{"child=0px 40px|no-repeat|border-box|text"});
    CHECK_EQ(logged(page, "clip="), std::string{"clip=border-area text"});
    CHECK_EQ(logged(page, "reset="), std::string{"reset=repeat|border-box"});
}

void test_background_substitution_failures() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<style>
        #invalid { background: red none center / 20px no-repeat;
                   background: image-set(attr(data-url)); }
        #missing { background: red none center / 20px no-repeat; background: var(--missing); }
        #parent { --empty: ; --inherited: blue; }
        #keywords { --alias: var(--x) green; --empty: ;
                    --initial: var(--empty) initial; --inherited: var(--empty) inherit;
                    background: var(--alias); }
        @layer a { #keywords { --x: ; } }
        @layer b { #keywords { --x: var(--empty) revert-layer; } }
        </style><div id="invalid" data-url="https://example.invalid/image.png"></div>
        <div id="missing"></div><div id="parent"><div id="keywords"></div></div><script>
        for (const id of ['invalid', 'missing']) {
            const s = getComputedStyle(document.getElementById(id));
            console.log(id + '=' + s.backgroundImage + '|' + s.backgroundColor + '|' +
                        s.backgroundSize + '|' + s.backgroundRepeat);
        }
        const target = document.getElementById('keywords');
        console.log('keyword=' + getComputedStyle(target).backgroundColor);
        target.style.color = 'var(--initial, red)';
        console.log('initial=' + getComputedStyle(target).color);
        target.style.color = 'var(--inherited)';
        console.log('inherit=' + getComputedStyle(target).color);
        for (const value of ['initial', 'var(--empty) initial', 'attr(missing, initial)']) {
            target.style.setProperty('--invalid', value);
            console.log('serialized-' + value + '=' + getComputedStyle(target).getPropertyValue('--invalid'));
        }
        target.style.setProperty('--loop', 'var(--loop, initial)');
        console.log('cycle=' + getComputedStyle(target).getPropertyValue('--loop'));
    </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    for (const std::string id : {"invalid", "missing"}) {
        CHECK_EQ(logged(page, id + "="), id + "=none|rgba(0, 0, 0, 0)|auto|repeat");
    }
    CHECK_EQ(logged(page, "keyword="), std::string{"keyword=rgb(0, 128, 0)"});
    CHECK_EQ(logged(page, "initial="), std::string{"initial=rgb(255, 0, 0)"});
    CHECK_EQ(logged(page, "inherit="), std::string{"inherit=rgb(0, 0, 255)"});
    for (const std::string value : {"initial", "var(--empty) initial", "attr(missing, initial)"}) {
        CHECK_EQ(logged(page, "serialized-" + value + "="), "serialized-" + value + "=");
    }
    CHECK_EQ(logged(page, "cycle="), std::string{"cycle="});
}

void test_image_color_scheme() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<div id="target"></div><script>
        const target = document.getElementById('target');
        for (const scheme of ['light', 'dark']) {
            target.style.colorScheme = scheme;
            target.style.backgroundImage = 'light-dark(image(red), image(blue))';
            console.log(scheme + '=' + getComputedStyle(target).backgroundImage);
            target.style.backgroundImage = 'linear-gradient(light-dark(red, blue), white)';
            console.log(scheme + '-gradient=' + getComputedStyle(target).backgroundImage);
            target.style.backgroundImage = 'cross-fade(light-dark(red, blue), image(white))';
            console.log(scheme + '-fade=' + getComputedStyle(target).backgroundImage);
            target.style.backgroundImage = 'light-dark(image(red), none)';
            console.log(scheme + '-none=' + getComputedStyle(target).backgroundImage);
        }
        </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    for (const std::string scheme : {"light", "dark"}) {
        const std::string color = scheme == "dark" ? "rgb(0, 0, 255)" : "rgb(255, 0, 0)";
        CHECK_EQ(logged(page, scheme + "="), scheme + "=image(" + color + ")");
        CHECK_EQ(logged(page, scheme + "-gradient="),
                 scheme + "-gradient=linear-gradient(" + color + ", rgb(255, 255, 255))");
        CHECK_EQ(logged(page, scheme + "-fade="),
                 scheme + "-fade=cross-fade(" + color + ", image(rgb(255, 255, 255)))");
        CHECK_EQ(logged(page, scheme + "-none="),
                 scheme + "-none=" +
                     (scheme == "dark" ? "image(rgba(0, 0, 0, 0))" : "image(rgb(255, 0, 0))"));
    }
}

void test_computed_radial_size_ranges() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<div id="target" style="font-size: 40px"></div><script>
        const target = document.getElementById('target');
        for (const property of ['background-image', 'mask-image', 'border-image-source',
                                'list-style-image']) {
            const gradient = 'radial-gradient(calc(10px - 0.5em) calc(0.5em + 10px)' +
                             ' at calc(10px - 1em) 30px, red, blue)';
            target.style.setProperty(property, gradient);
            console.log(property + '=' + getComputedStyle(target).getPropertyValue(property));
            target.style.setProperty('--image', gradient);
            target.style.setProperty(property, 'var(--image)');
            console.log(property + '-var=' + getComputedStyle(target).getPropertyValue(property));
            target.style.setProperty('--image', 'radial-gradient(-10px 30px, red, blue)');
            console.log(property + '-invalid=' + getComputedStyle(target).getPropertyValue(property));
            target.style.setProperty('--image', 'radial-gradient(-10px calc(10px + 20%), red, blue)');
            console.log(property + '-invalid-mixed=' + getComputedStyle(target).getPropertyValue(property));
            target.style.setProperty(property, 'radial-gradient(calc(10% - 20%) 30px, red, blue)');
            console.log(property + '-percent=' + getComputedStyle(target).getPropertyValue(property));
        }
        </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    for (const std::string property :
         {"background-image", "mask-image", "border-image-source", "list-style-image"}) {
        const std::string gradient =
            "radial-gradient(0px 30px at -30px 30px, rgb(255, 0, 0), rgb(0, 0, 255))";
        CHECK_EQ(logged(page, property + "="), property + "=" + gradient);
        CHECK_EQ(logged(page, property + "-var="), property + "-var=" + gradient);
        CHECK_EQ(logged(page, property + "-invalid="), property + "-invalid=none");
        CHECK_EQ(logged(page, property + "-invalid-mixed="), property + "-invalid-mixed=none");
        CHECK_EQ(logged(page, property + "-percent="),
                 property + "-percent=radial-gradient(0% 30px, rgb(255, 0, 0), rgb(0, 0, 255))");
    }
}

void test_computed_layer_size_ranges() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<style>
        #parent { font-size: 40px;
                  background-size: calc(10px + 0.5em) calc(10px - 0.5em), 1em auto;
                  mask-size: calc(10px - 0.5em) calc(10px + 0.5em); }
        #child { font-size: 10px; background-size: inherit; mask-size: inherit; }
        </style><div id="parent"><div id="child"></div></div><script>
        const p = document.getElementById('parent');
        for (const id of ['parent', 'child']) {
            const cs = getComputedStyle(document.getElementById(id));
            console.log(id + '=' + cs.backgroundSize + '|' + cs.maskSize);
        }
        for (const property of ['background-size', 'mask-size']) {
            p.style.setProperty(property, 'calc(10px - 0.5em) calc(10px + 0.5em)');
            console.log(property + '-inline=' + getComputedStyle(p).getPropertyValue(property));
            p.style.setProperty('--size', 'calc(10px + 0.5em) calc(10px - 0.5em)');
            p.style.setProperty(property, 'var(--size)');
            console.log(property + '-variable=' + getComputedStyle(p).getPropertyValue(property));
            p.style.setProperty('--size', '-10px calc(10px + 0.5em)');
            console.log(property + '-invalid=' + getComputedStyle(p).getPropertyValue(property));
            p.style.setProperty('--size', '-10px calc(10px + 20%)');
            console.log(property + '-invalid-mixed=' + getComputedStyle(p).getPropertyValue(property));
            p.style.setProperty(property, 'calc(10% - 20%) calc(10px - 20%)');
            console.log(property + '-percent=' + getComputedStyle(p).getPropertyValue(property));
        }
        p.style.backgroundSize = 'random(1px, 1000px) random(1px, 1000px)';
        const sizes = getComputedStyle(p).backgroundSize.split(' ');
        console.log('random=' + (sizes[0] !== sizes[1]));
        p.style.backgroundSize = 'calc(infinity * 1px) auto';
        console.log('infinity=' + getComputedStyle(p).backgroundSize);
        </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "parent="), std::string{"parent=30px 0px, 40px auto|0px 30px"});
    CHECK_EQ(logged(page, "child="), std::string{"child=30px 0px, 40px auto|0px 30px"});
    for (const std::string property : {"background-size", "mask-size"}) {
        CHECK_EQ(logged(page, property + "-inline="), property + "-inline=0px 30px");
        CHECK_EQ(logged(page, property + "-variable="), property + "-variable=30px 0px");
        CHECK_EQ(logged(page, property + "-invalid="), property + "-invalid=auto");
        CHECK_EQ(logged(page, property + "-invalid-mixed="), property + "-invalid-mixed=auto");
        CHECK_EQ(logged(page, property + "-percent="), property + "-percent=0% calc(-20% + 10px)");
    }
    CHECK_EQ(logged(page, "random="), std::string{"random=true"});
    CHECK_EQ(logged(page, "infinity="), std::string{"infinity=33554432px auto"});
}

void test_nonnegative_computed_widths() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<style>
        #parent { font-size: 40px; border-style: solid;
                  border-width: calc(10px - 0.5em) 1em;
                  border-image-width: calc(10px - 0.5em) 1em 2 auto;
                  border-image-outset: 0 calc(0.5em + 10px) 3 calc(10px - 0.5em); }
        #child { font-size: 10px; border-image-width: inherit; border-image-outset: inherit; }
        </style><div id="parent"><div id="child"></div></div>
        <script>
        const p = document.getElementById('parent');
        console.log('sheet=' + getComputedStyle(p).borderWidth);
        p.style.borderBottomWidth = 'calc(10px - 0.5em)';
        console.log('inline=' + getComputedStyle(p).borderBottomWidth);
        p.style.setProperty('--w', 'calc(10px - 0.5em)');
        p.style.borderLeftWidth = 'var(--w)';
        console.log('variable=' + getComputedStyle(p).borderLeftWidth);
        p.style.borderWidth = 'var(--w)';
        console.log('variable-shorthand=' + getComputedStyle(p).borderWidth);
        for (const id of ['parent', 'child']) {
            const cs = getComputedStyle(document.getElementById(id));
            console.log(id + '=' + cs.borderImageWidth + '|' + cs.borderImageOutset);
        }
        p.style.borderImage = 'none 20 / calc(10px - 0.5em) / 1em round';
        const cs = getComputedStyle(p);
        console.log('shorthand=' + cs.borderImageWidth + '|' + cs.borderImageOutset + '|' +
                    cs.borderImageRepeat);
        p.style.borderImageWidth = '1em 40px';
        console.log('compressed=' + getComputedStyle(p).borderImageWidth);
        p.style.setProperty('--image-width', 'calc(10px - 0.5em) 1em');
        p.style.borderImageWidth = 'var(--image-width)';
        console.log('image-variable=' + getComputedStyle(p).borderImageWidth);
        p.style.setProperty('--image', '20 / calc(10px - 0.5em) / 1em round');
        p.style.borderImage = 'var(--image)';
        console.log('image-variable-shorthand=' + getComputedStyle(p).borderImageWidth + '|' +
                    getComputedStyle(p).borderImageOutset);
        p.style.setProperty('--image-width', '-10px 1em');
        p.style.borderImageWidth = 'var(--image-width)';
        console.log('image-invalid-variable=' + getComputedStyle(p).borderImageWidth);
        </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "sheet="), std::string{"sheet=0px 40px"});
    CHECK_EQ(logged(page, "inline="), std::string{"inline=0px"});
    CHECK_EQ(logged(page, "variable="), std::string{"variable=0px"});
    CHECK_EQ(logged(page, "variable-shorthand="), std::string{"variable-shorthand=0px"});
    CHECK_EQ(logged(page, "parent="), std::string{"parent=0px 40px 2 auto|0 30px 3 0px"});
    CHECK_EQ(logged(page, "child="), std::string{"child=0px 40px 2 auto|0 30px 3 0px"});
    CHECK_EQ(logged(page, "shorthand="), std::string{"shorthand=0px|40px|round"});
    CHECK_EQ(logged(page, "compressed="), std::string{"compressed=40px"});
    CHECK_EQ(logged(page, "image-variable="), std::string{"image-variable=0px 40px"});
    CHECK_EQ(logged(page, "image-variable-shorthand="),
             std::string{"image-variable-shorthand=0px|40px"});
    CHECK_EQ(logged(page, "image-invalid-variable="), std::string{"image-invalid-variable=1"});
}

void test_elliptical_radius_cascade() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<style>
        #parent { font-size: 20px; border-radius: 1em 2em / 3px 4%; }
        #child { font-size: 40px; border-radius: inherit; border-top-left-radius: 7px 8px; }
        #variable { --r: 1em 2em / calc(1px + 2px) 4%; font-size: 20px;
                    border-radius: var(--r); }
        #negative { font-size: 20px; border-top-left-radius: calc(1px - 2px) 2em; }
        </style><div id="parent"><div id="child"></div></div><div id="variable"></div>
        <div id="negative"></div>
        <script>
        for (const id of ['parent', 'child', 'variable']) {
            const cs = getComputedStyle(document.getElementById(id));
            console.log(id + '=' + cs.borderRadius + '|' + cs.borderTopLeftRadius + '|' +
                        cs.borderBottomLeftRadius);
        }
        const p = document.getElementById('parent');
        p.style.borderRadius = '5px / 6px';
        console.log('inline=' + getComputedStyle(p).borderRadius);
        p.style.borderBottomLeftRadius = '9px';
        console.log('override=' + getComputedStyle(p).borderRadius);
        console.log('negative=' + getComputedStyle(document.getElementById('negative')).borderTopLeftRadius);
        </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "parent="), std::string{"parent=20px 40px / 3px 4%|20px 3px|40px 4%"});
    CHECK_EQ(logged(page, "child="),
             std::string{"child=7px 40px 20px / 8px 4% 3px|7px 8px|40px 4%"});
    CHECK_EQ(logged(page, "variable="),
             std::string{"variable=20px 40px / 3px 4%|20px 3px|40px 4%"});
    CHECK_EQ(logged(page, "inline="), std::string{"inline=5px / 6px"});
    CHECK_EQ(logged(page, "override="), std::string{"override=5px 5px 5px 9px / 6px 6px 6px 9px"});
    CHECK_EQ(logged(page, "negative="), std::string{"negative=0px 40px"});
}

// Reflection and hasOwnProperty share the live indexed properties, including
// mutations that bypass the wrappers, and retain explicitly defined properties.
void test_live_own_descriptors() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<div id="e" class="x y" style="color: red"></div><script>
        const e = document.getElementById('e');
        const style = e.style, tokens = e.classList;
        style.expando = undefined;
        function describe(object, key) {
            const d = Object.getOwnPropertyDescriptor(object, key);
            return d ? [d.value, d.writable, d.enumerable, d.configurable].join(':') : 'absent';
        }
        function own(object, key) { return Object.prototype.hasOwnProperty.call(object, key); }
        console.log('indices=' + describe(style, '0') + ',' + describe(tokens, '1'));
        console.log('own=' + Object.hasOwn(style, '0') + ',' +
                    own(style, 'expando') + ',' + Object.hasOwn(style, 'expando') + ',' +
                    own(style, '01') + ',' +
                    own(tokens, '1') + ',' + own(tokens, '01'));
        e.setAttribute('style', 'width: 1px');
        e.className = 'z';
        console.log('live=' + describe(style, '0') + ',' + describe(tokens, '0') + ',' +
                    describe(tokens, '1'));
        e.removeAttribute('style');
        e.removeAttribute('class');
        console.log('removed=' + own(style, '0') + ',' + own(tokens, '0'));
        Object.defineProperty(style, 'expando', {
            value: 'x', writable: false, enumerable: false, configurable: false
        });
        console.log('defined=' + describe(style, 'expando') + ',' + own(style, 'expando'));
    </script>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "indices="),
             std::string{"indices=color:false:true:true,y:false:true:true"});
    CHECK_EQ(logged(page, "own="), std::string{"own=true,true,true,false,true,false"});
    CHECK_EQ(logged(page, "live="),
             std::string{"live=width:false:true:true,z:false:true:true,absent"});
    CHECK_EQ(logged(page, "removed="), std::string{"removed=false,false"});
    CHECK_EQ(logged(page, "defined="), std::string{"defined=x:false:false:false,true"});
}

// CSS-namespace-object-class-string, CSSStyleDeclaration-iterator, escape,
// cssom-fontfacerule-constructors: the class strings, the iterator and the
// namespace object's own tag and arities.
void test_class_strings_and_iterators() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>.a { color: red; width: 1px }</style></head><body><script>
        const rule = document.styleSheets[0].cssRules[0];
        console.log('tag=' + Object.prototype.toString.call(rule) + ',' +
                    Object.prototype.toString.call(rule.style) + ',' + CSS.toString());
        console.log('iter=' + (Symbol.iterator in CSSStyleDeclaration.prototype) + ',' +
                    rule.style[Symbol.iterator]().next().value + ',' +
                    (Symbol.iterator in CSSRuleList.prototype));
        let caught = '';
        try { CSS.escape(); } catch (e) { caught = e.name; }
        console.log('escape=' + CSS.escape.length + ',' + caught + ',' +
                    CSS.hasOwnProperty(Symbol.toStringTag));
        // DOMTokenList is iterable<DOMString>: keys/values/entries/forEach and
        // @@iterator, live over the attribute.
        const cl = document.body.classList; document.body.className = 'x y';
        const seen = []; cl.forEach((t, i) => seen.push(i + t));
        document.body.className = 'x y z';
        console.log('tokens=' + [...cl.keys()].join() + '|' + [...cl.values()].join() + '|' +
                    [...cl.entries()].map(e => e.join(':')).join() + '|' + seen.join() + '|' +
                    (DOMTokenList.prototype.forEach === Array.prototype.forEach));
        // The other token-list attributes: relList, output.htmlFor, iframe.sandbox, link.sizes.
        const a = document.createElement('a'); a.rel = 'noopener nofollow';
        const o = document.createElement('output'); o.htmlFor = 'x y';
        console.log('lists=' + a.relList.length + ',' + a.relList.contains('nofollow') + ',' +
                    Object.prototype.toString.call(document.createElement('iframe').sandbox) + ',' +
                    o.getAttribute('for') + ',' + document.createElement('link').sizes.length);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "tokens="), std::string{"tokens=0,1,2|x,y,z|0:x,1:y,2:z|0x,1y|true"});
    CHECK_EQ(logged(page, "lists="), std::string{"lists=2,true,[object DOMTokenList],x y,0"});
    CHECK_EQ(logged(page, "tag="),
             std::string{"tag=[object CSSStyleRule],[object CSSStyleProperties],[object CSS]"});
    CHECK_EQ(logged(page, "iter="), std::string{"iter=true,color,true"});
    CHECK_EQ(logged(page, "escape="), std::string{"escape=1,TypeError,true"});
}

// cssom-pagerule "after rule was removed", insertRule-charset-no-index,
// CSSKeyframesRule, CSSContainerRule: the rule side.
void test_removed_rules_charset_keyframes_and_container() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>@charset "UTF-8";
        @keyframes k { 0% { top: 0px; } 100% { top: 200px; } }
        @page { margin-top: 1px }</style></head><body><script>
        const sheet = document.styleSheets[0];
        console.log('charset=' + sheet.cssRules.length + ',' + sheet.cssRules[0].name);
        let caught = '';
        try { sheet.insertRule('@charset "x";'); } catch (e) { caught = e.name; }
        console.log('charsetInsert=' + caught);
        const k = sheet.cssRules[0];
        console.log('indexed=' + k[0].cssText + '|' + k.length);
        k.name = 'none';
        console.log('named=' + k.cssText.replace(/\s/g, ''));
        const p = sheet.cssRules[1];
        sheet.deleteRule(1);
        p.selectorText = 'named';
        console.log('detached=' + p.parentStyleSheet + ',' + p.cssText);
        sheet.insertRule('@container side (min-width: 100px) {}', 0);
        const c = sheet.cssRules[0];
        console.log('container=' + (c instanceof CSSContainerRule) + ',' + c.containerName + ',' +
                    c.containerQuery + ',' + c.conditionText);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    // `@charset` is consumed before the rule list: rule zero is the keyframes.
    CHECK_EQ(logged(page, "charset="), std::string{"charset=2,k"});
    CHECK_EQ(logged(page, "charsetInsert="), std::string{"charsetInsert=SyntaxError"});
    CHECK_EQ(logged(page, "indexed="), std::string{"indexed=0% { top: 0px; }|2"});
    CHECK_EQ(logged(page, "named="),
             std::string{"named=@keyframes\"none\"{0%{top:0px;}100%{top:200px;}}"});
    CHECK_EQ(logged(page, "detached="),
             std::string{"detached=null,@page named { margin-top: 1px; }"});
    CHECK_EQ(logged(page, "container="),
             std::string{"container=true,side,(min-width: 100px),side (min-width: 100px)"});
}

// rule-restrictions, page-descriptors, cssstyledeclaration-cssfontrule,
// cssstyledeclaration-csstext-important, setproperty-null-undefined: the
// three declaration blocks and what each takes.
void test_declaration_blocks() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @page { margin-top: 10px; transform: scale(1) }
        @font-face { src: url(x.ttf) }
        @keyframes k { from { top: 0px; animation-name: k } }
        .a { color: red; color: blue; padding: 1px !important; padding: 2px }
        </style></head><body><script>
        const rules = document.styleSheets[0].cssRules;
        const pg = rules[0].style;
        pg.setProperty('transform', 'scale(1)');
        pg.cssText = 'margin-bottom: 1px; transform: scale(1)';
        console.log('page=' + pg.length + ',' + pg.cssFloat + ',' + pg.marginBottom + ',' +
                    Object.prototype.toString.call(pg));
        const ff = rules[1].style;
        console.log('font=' + ff.src.includes('x.ttf') + ',' + ('unicodeRange' in ff) + ',' + ff.color + ',' +
                    Object.prototype.toString.call(ff));
        const kf = rules[2].cssRules[0].style;
        kf.setProperty('animation-name', 'k');
        console.log('keyframe=' + kf.length + ',' + kf.top);
        const st = rules[3].style;
        console.log('dup=' + st.cssText);
        st.setProperty('color', null);
        console.log('nulled=' + st.color + '|' + st.length);
        st.setProperty('color', 'green', undefined);
        st.setProperty('width', 'undefined');
        console.log('undef=' + st.color + '|' + st.width);
        // cssstyledeclaration-csstext: a name that is not a supported property
        // has no IDL setter - the write is an ordinary property, never a declaration.
        const el = document.body.style;
        el.COLOR = 'red'; el.unknown = 'unknown'; el.color = 'red'; el.fontSize = '10pt';
        console.log('expando=' + el.cssText + '|' + el.unknown + '|' + el.COLOR + '|' + el.length);
        // `in`: every supported property in both spellings, set or not (CSSOM
        // 6.7.2), the indices below length, the expandos - and no custom
        // property or unknown name.
        console.log('in=' + [('zIndex' in el), ('z-index' in el), ('webkitTransform' in el),
                    ('WebkitTransform' in el), ('unknown' in el), ('nothing' in el),
                    ('--x' in el), (1 in el), (2 in el), ('setProperty' in el)].join());
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    // `transform` is not a page-context property; `cssFloat` is not a page
    // descriptor - the block is a CSSPageDescriptors, not a CSSStyleProperties.
    CHECK_EQ(logged(page, "page="),
             std::string{"page=1,undefined,1px,[object CSSPageDescriptors]"});
    CHECK_EQ(logged(page, "font="),
             std::string{"font=true,true,undefined,[object CSSFontFaceDescriptors]"});
    CHECK_EQ(logged(page, "keyframe="), std::string{"keyframe=1,0px"});
    // The later declaration wins, unless the earlier was important.
    CHECK_EQ(logged(page, "dup="), std::string{"dup=color: blue; padding: 1px !important;"});
    CHECK_EQ(logged(page, "nulled="), std::string{"nulled=|4"});
    CHECK_EQ(logged(page, "undef="), std::string{"undef=green|"});
    CHECK_EQ(logged(page, "expando="),
             std::string{"expando=color: red; font-size: 10pt;|unknown|red|2"});
    CHECK_EQ(logged(page, "in="),
             std::string{"in=true,true,true,true,true,false,false,true,false,true"});
}

// style-sheet-interfaces-001, stylesheet-same-origin: `sheet` on the
// prototype, and a cross-origin sheet that will not show its rules.
void test_sheet_attribute_and_origin() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style id=s>.a { color: red }</style>
        <link id=far rel=stylesheet href="http://other.invalid/x.css">
        <link id=near rel=stylesheet href="missing.css"></head><body><script>
        const s = document.getElementById('s');
        console.log('proto=' + !s.hasOwnProperty('sheet') + ',' +
                    ('sheet' in HTMLStyleElement.prototype) + ',' +
                    (s.sheet === document.styleSheets[0]));
        let caught = '';
        try { document.getElementById('far').sheet.cssRules; } catch (e) { caught = e.name; }
        let inserted = '';
        try { document.getElementById('far').sheet.insertRule('a {}'); } catch (e) { inserted = e.name; }
        console.log('far=' + caught + ',' + inserted + ',' +
                    document.getElementById('near').sheet.cssRules.length);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "proto="), std::string{"proto=true,true,true"});
    CHECK_EQ(logged(page, "far="), std::string{"far=SecurityError,SecurityError,0"});
}

// getComputedStyle-resolved-colors: a system colour resolves to an rgb(),
// and a logical border longhand answers as its physical one.
void test_resolved_colours() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        #t { background-color: Menu; border: 1px solid Menu; color: Menu; caret-color: Menu;
             box-shadow: 1px 1px Menu, inset 2px 3px 4px; text-shadow: 1em 0 red }
        #u { color: blue; box-shadow: 0 0 0 1px currentcolor }
        </style></head><body><div id=t></div><div id=u></div><script>
        const cs = getComputedStyle(document.getElementById('t'));
        console.log('sys=' + ['background-color', 'border-top-color', 'border-block-end-color',
                              'border-inline-start-color', 'color', 'caret-color',
                              'border-block-start-width', 'border-inline-end-style']
                        .map(p => cs.getPropertyValue(p)).join('|'));
        console.log('shadow=' + cs.boxShadow + '|' + cs.textShadow + '|' +
                    getComputedStyle(document.getElementById('u')).boxShadow);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "sys="),
             std::string{"sys=rgb(247, 247, 247)|rgb(247, 247, 247)|rgb(247, 247, 247)|"
                         "rgb(247, 247, 247)|rgb(247, 247, 247)|rgb(247, 247, 247)|1px|solid"});
    // A shadow's colour first and resolved - the system colour, an omitted
    // one as the element's own `color` - and its lengths in px, padded.
    CHECK_EQ(logged(page, "shadow="),
             std::string{"shadow=rgb(247, 247, 247) 1px 1px 0px 0px, "
                         "rgb(247, 247, 247) 2px 3px 4px 0px inset|"
                         "rgb(255, 0, 0) 16px 0px 0px|rgb(0, 0, 255) 0px 0px 0px 1px"});
}

// css-multicol/inheritance and column-rule-computed: line-width keywords
// compute to lengths even when the rule is hidden, and survive shorthand folding.
void test_column_rule_widths() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><div id=t style="color: lime"></div><script>
        const cs = getComputedStyle(t);
        console.log('initial=' + cs.columnRuleWidth);
        console.log('widths=' + ['none', 'hidden', 'solid'].map(style => {
            t.style.columnRuleStyle = style;
            return ['thin', 'medium', 'thick'].map(width => {
                t.style.columnRuleWidth = width;
                return cs.columnRuleWidth;
            }).join();
        }).join('|'));
        console.log('rules=' + ['dotted', 'medium hidden currentcolor',
                                'medium solid currentcolor', '10px'].map(rule => {
            t.style.columnRule = rule;
            return cs.columnRule;
        }).join('|'));
        console.log('specified=' + t.style.columnRuleWidth + '|' + t.style.columnRule);
        console.log('borders=' + ['none', 'hidden', 'solid'].map(style => {
            t.style.border = 'medium ' + style;
            t.style.outline = 'medium ' + style;
            return cs.borderTopWidth + ',' + cs.outlineWidth;
        }).join('|'));
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "initial="), std::string{"initial=3px"});
    CHECK_EQ(logged(page, "widths="), std::string{"widths=1px,3px,5px|1px,3px,5px|1px,3px,5px"});
    CHECK_EQ(logged(page, "rules="),
             std::string{"rules=3px dotted rgb(0, 255, 0)|3px hidden rgb(0, 255, 0)|"
                         "3px solid rgb(0, 255, 0)|10px rgb(0, 255, 0)"});
    CHECK_EQ(logged(page, "specified="), std::string{"specified=10px|10px"});
    // A border's width is zero beside `none` and `hidden`; an outline's is
    // not (CSS UI 4 §5.2, outline-width-computed).
    CHECK_EQ(logged(page, "borders="), std::string{"borders=0px,3px|0px,3px|3px,3px"});
}

// getComputedStyle-detached-subtree, getComputedStyle-pseudo-with-argument: an
// element outside the flat tree - a light child of a shadow host that no
// `<slot>` takes - has no computed style, while a slotted one keeps its own;
// a node of another document answers the same empty declaration; and a
// functional pseudo-element argument parses as the selector grammar does.
void test_flat_tree_and_pseudo_arguments() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body>
        <div id=host><div id=unslotted></div></div>
        <div id=slotting><div id=slotted></div><div id=named slot=n></div></div>
        <iframe id=f srcdoc="<html></html>" style="display: none"></iframe>
        <script>
        host.attachShadow({mode: 'open'});
        slotting.attachShadow({mode: 'open'}).innerHTML = '<slot></slot>';
        const empty = el => getComputedStyle(el).length == 0 && getComputedStyle(el).color === '';
        console.log('flat=' + empty(unslotted) + ',' + empty(slotted) + ',' + empty(named) + ',' +
                    empty(host) + ',' + empty(f.contentDocument.documentElement));
        const parses = p => getComputedStyle(host, p).length != 0;
        console.log('fn=' + ['::highlight(name)', '::highlight( n\\61me ', '::picker(select)',
                             '::view-transition-old(x)']
                        .map(parses).join() + '|' +
                    ['::highlight()', '::highlight(1)', '::highlight(name)a', ':highlight(name)',
                     '::picker(div)', '::before(x)', '::highlight (name)']
                        .map(parses).join());
        // getComputedStyle-pseudo: a rule for `::highlight(name)` IS the
        // answer for that pseudo-element, and not for another argument.
        const sheet = document.createElement('style');
        sheet.textContent = '#host::highlight(name) { color: rgb(0, 128, 0) }'
            + ' #host::view-transition-old(x) { color: rgb(0, 0, 128) }';
        document.documentElement.insertBefore(sheet, document.body); // the page has no <head>
        console.log('hl=' + getComputedStyle(host, '::highlight(name)').color + '|' +
                    getComputedStyle(host, '::highlight(other)').color + '|' +
                    getComputedStyle(host, '::view-transition-old(x)').color + '|' +
                    document.styleSheets[0].cssRules[0].selectorText);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "flat="), std::string{"flat=true,false,true,false,true"});
    CHECK_EQ(logged(page, "hl="),
             std::string{"hl=rgb(0, 128, 0)|rgb(0, 0, 0)|rgb(0, 0, 128)|#host::highlight(name)"});
    CHECK_EQ(logged(page, "fn="),
             std::string{"fn=true,true,true,true|false,false,false,false,false,false,false"});
}

// getComputedStyle-sticky-pos-percent: a sticky inset's percentage resolves
// against the nearest scroll container's content box, `auto` stays `auto`,
// and a calc() folds against the same basis.
void test_sticky_insets() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body style="margin: 0">
        <div style="height: 500px; overflow: hidden">
          <div style="height: 400px">
            <div id="t" style="height: 100px; position: sticky; left: 0; top: 50%;
                               bottom: calc(10% - 1px);"></div>
          </div>
        </div>
        <script>
        const cs = getComputedStyle(document.getElementById('t'));
        console.log('sticky=' + cs.top + ',' + cs.bottom + ',' + cs.left + ',' + cs.right);
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "sticky="), std::string{"sticky=250px,49px,0px,auto"});
}

// mediaquery-sort-dedup: matchMedia serialises the list as written - not
// sorted, not deduplicated - and answers `matches` from the cascade's own
// environment.
void test_match_media() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><script>
        console.log('mm=' + matchMedia('(min-width: 10px) and (min-height: 10px)').media + '|' +
                    window.matchMedia('(color) and (color)').media + '|' +
                    matchMedia('(min-width: 300px)').matches + ',' +
                    matchMedia('(min-width: 500px)').matches + ',' +
                    matchMedia('screen').matches + ',' + matchMedia('print').matches + '|' +
                    Object.prototype.toString.call(matchMedia('all')));
        // Media Queries 4: a length may be any unit (em is the initial 16px,
        // the viewport units are the viewport's) or a math function of them.
        console.log('units=' + [matchMedia('(width: 100vw)').matches,
                                matchMedia('(height: 100vh)').matches,
                                matchMedia('(width: calc(50vw + 200px))').matches,
                                matchMedia('(width: calc(200vh + 5em))').matches,
                                matchMedia('(min-width: 25em)').matches,
                                matchMedia('(min-width: 26em)').matches,
                                matchMedia('(width >= calc(100vh * 2))').matches].join());
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "mm="),
             std::string{"mm=(min-width: 10px) and (min-height: 10px)|(color) and (color)|"
                         "true,false,true,false|[object MediaQueryList]"});
    // 400 x 200: 200vh + 5em is 480 (false), 100vh * 2 is 400.
    CHECK_EQ(logged(page, "units="), std::string{"units=true,true,true,false,true,false,true"});
}

// ttwf-cssom-doc-ext-load-count: a StyleSheetList held in a variable is live -
// its `length` follows a removed <style> - and it still iterates, indexes and
// answers `item()`, and is one object.
void test_live_sheet_list() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>a { color: red }</style><style></style></head><body>
        <script>
        const list = document.styleSheets;
        const before = list.length + ',' + [...list].length + ',' + Array.from(list).length;
        list.item(0).ownerNode.remove();
        console.log('live=' + before + '|' + list.length + ',' + [...list].length + ',' +
                    (list.item(1) === null) + ',' + (list[0] === document.styleSheets[0]) + ',' +
                    (list === document.styleSheets) + ',' + (list instanceof StyleSheetList) + ',' +
                    Object.prototype.toString.call(list));
        </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "live="),
             std::string{"live=2,2,2|1,1,true,true,true,true,[object StyleSheetList]"});
}

// tree-counting/sibling-function-descriptors: a descriptor is on no element,
// so a tree-counting function in one is invalid and the earlier value stays.
void test_descriptors_refuse_tree_counting() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @page { margin-top: 10px; margin-top: calc(0px * sibling-index()) }
        @font-face { font-weight: 300; font-weight: calc(max(0 * sibling-count(), 400)) }
        </style></head><body><script>
        const rules = document.styleSheets[0].cssRules;
        rules[0].style.setProperty('margin-bottom', 'calc(1px * sibling-index())');
        console.log('desc=' + rules[0].style.marginTop + '|' + rules[0].style.marginBottom + '|' +
                    rules[1].style.fontWeight);
    </script></body></html>)");
    CHECK_EQ(page.script_error(), std::string{});
    CHECK_EQ(logged(page, "desc="), std::string{"desc=10px||300"});
}

} // namespace

int main() {
    test_computed_background_positions();
    test_background_cascade();
    test_background_substitution_failures();
    test_image_color_scheme();
    test_computed_radial_size_ranges();
    test_computed_layer_size_ranges();
    test_nonnegative_computed_widths();
    test_elliptical_radius_cascade();
    test_resolved_colours();
    test_column_rule_widths();
    test_flat_tree_and_pseudo_arguments();
    test_live_sheet_list();
    test_sticky_insets();
    test_match_media();
    test_descriptors_refuse_tree_counting();
    test_class_strings_and_iterators();
    test_live_own_descriptors();
    test_removed_rules_charset_keyframes_and_container();
    test_declaration_blocks();
    test_sheet_attribute_and_origin();
    REPORT("cssom_wpt");
}
