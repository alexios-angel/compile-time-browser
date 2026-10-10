//===- ctbrowser/page/author_styles.hpp - A document's author sheets -*- C++ -*-===//
//
// The author's style sheets as one text for the cascade: every <style> and
// <link rel=stylesheet> in document order, @import expanded in place, each
// sheet gated by its `media` attribute. Also the two rules the CSSOM shares
// with that walk (which <link>s are stylesheets, and how an import's href
// resolves), and the container-size answer the cascade asks of layout.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/shell/page/assets.hpp>
#include <ctbrowser/style/style.hpp>

namespace ctbrowser::page {

/// What a `<link>` is to the cascade and the object model (HTML 4.6.7 and
/// 4.2.4.4). `rel` is a space-separated token list matched ASCII
/// case-insensitively; the `disabled` attribute keeps the sheet from being
/// obtained at all; an `alternate stylesheet` is fetched (its `load` fires) but
/// does not apply and is not in `document.styleSheets` unless the link was
/// explicitly enabled, the flag `link.disabled = false` sets when it removes
/// the attribute.
enum class link_sheet {
    none,
    alternate,
    active
};

/// Classifies a <link> as an active, alternate or non-stylesheet.
[[nodiscard]] link_sheet link_sheet_state(std::string_view rel, bool disabled_attribute,
                                          bool explicitly_enabled);

/// Where `reference` (an @import or a nested sheet's href) points, seen from
/// a sheet at `base`: unchanged when absolute, a data: URL or rooted; resolved
/// as a URL against an absolute base; otherwise beside the base's path, which
/// the asset registry resolves from the page's directory.
[[nodiscard]] std::string resolve_sheet_href(std::string_view base, std::string_view reference);

/// What collect_author_styles asks of a caller that tracks more than the
/// document does. Every hook may be left empty.
struct author_style_hooks {
    /// The preferred style sheet set's title (HTML 4.2.6) when the caller keeps
    /// one. Empty means the first titled sheet of the walk is preferred, which
    /// is the same answer for a parsed document.
    std::function<std::string_view()> preferred_title;
    /// Whether a <link> was explicitly enabled. Empty means none was.
    std::function<bool(node_id)> link_enabled;
    /// Told, in document order, that a sheet loaded (true) or that a <link>'s
    /// href gave nothing (false).
    std::function<void(node_id, bool)> loaded;
    /// Told the href of a <link rel=stylesheet> that could not be read.
    std::function<void(const std::string &)> missing;
};

/// The author style text of `doc`, ready for one add_sheet at author origin.
/// Sheets are read through `assets` and decoded with the document's encoding
/// as the fallback (CSS Syntax 3 §3.2).
[[nodiscard]] std::string collect_author_styles(const document & doc, atom_table & atoms,
                                                const shell::asset_registry & assets,
                                                const author_style_hooks & hooks = {});

/// A query container's size, as the cascade asks it (CSS Conditional 5 §3.1):
/// the content box of `id`'s fragment in `fragments` as last laid out, and
/// which directions a scroll container can still scroll. Nothing for a box
/// size containment does not apply to, or when `id` has no fragment.
[[nodiscard]] std::optional<style::engine::container_size> container_size_of(
    const layout::fragment & fragments, atom_table & atoms, node_id id);

} // namespace ctbrowser::page
