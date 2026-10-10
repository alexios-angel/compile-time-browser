//===- ctcompile/test/CSS/ParseControl.cpp - Gate control: parses CSS -----===//
//
// The negative control for css_syntax.native's gate: links what that test
// links (ctbrowser's style library, no VM) and calls the tokenizer, so the
// gate's ban on parsing symbols must reject it. Without a control that only
// that clause can refuse, the gate could be green while seeing nothing.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/style/css/token.hpp>

int main() {
    return ctbrowser::style::css::tokenize("a{b:c}").tokens.empty() ? 1 : 0;
}
