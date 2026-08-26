#ifndef COMMENTCOLORSWATCH_H
#define COMMENTCOLORSWATCH_H

#include <functional>

#include <QColor>
#include <QIcon>
#include <QString>

namespace vnotex {

// Renders a CommentColor token as a small colour chip, for every colour picker
// in the comment feature (the PDF tool menus, the comment dock combo, and the
// page context menu).
//
// LEAF BY CONTRACT: this header and its .cpp reference NO ThemeService and no
// widget. The themed colour arrives as an injected RESOLVER instead, because a
// nullable `ThemeService *` would not help — `theme ? theme->x() : y` still
// emits a link-time reference to ThemeService from the CALLER's translation
// unit, and the test targets that compile commentpanel.cpp /
// pdfannotationtoolbar.cpp deliberately do not link themeservice.cpp.
//
// Colour here is DATA (the user picks it), painted with QPainter and never a
// stylesheet — the sanctioned exception to the no-hardcoded-colours rule, see
// src/widgets/AGENTS.md § No Hardcoded Colors in C++.
namespace CommentColorSwatch {

// token -> CSS colour string. An empty/default-constructed resolver means "use
// builtInColor()", which is what an unthemed caller (and every test) gets.
using ColorResolver = std::function<QString(const QString &p_token)>;

// SSOT for the built-in comment-highlight colours. ThemeService's
// commentHighlightColor() calls this rather than holding its own table; there
// must be exactly one table.
QString builtInColor(const QString &p_token);

// Parses "#RRGGBB", "#AARRGGBB", "rgb(r, g, b)" and "rgba(r, g, b, a)".
// Invalid input yields an invalid QColor.
QColor parseCssColor(const QString &p_css);

// Paints the chip. An unparseable resolved fill falls back to
// builtInColor(p_token), then to the default token's built-in — never to a
// null/black chip. @p_borderCss empty yields a neutral grey border.
QIcon icon(const ColorResolver &p_resolve, const QString &p_token, int p_sizePx = 16,
           const QString &p_borderCss = QString());

// Unthemed convenience == icon(ColorResolver(), p_token, p_sizePx).
QIcon icon(const QString &p_token, int p_sizePx = 16);

} // namespace CommentColorSwatch

} // namespace vnotex

#endif // COMMENTCOLORSWATCH_H
