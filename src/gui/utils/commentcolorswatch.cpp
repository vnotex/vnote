#include "commentcolorswatch.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>

#include <core/services/commenttypes.h>

using namespace vnotex;

namespace {

// Fallback border when the caller supplied no themed one. Neutral grey reads on
// both a light and a dark menu, and the chip is composited over white anyway.
const QColor c_fallbackBorder(160, 160, 160);

// The chip is composited over WHITE, not over the app palette: the tokens are
// translucent and are anchored to the PDF page, which pdf.js renders from the
// document and never tints with the theme.
const QColor c_groundColor(255, 255, 255);

QColor compositeOverWhite(const QColor &p_color) {
  const qreal alpha = p_color.alphaF();
  return QColor::fromRgbF(c_groundColor.redF() * (1.0 - alpha) + p_color.redF() * alpha,
                          c_groundColor.greenF() * (1.0 - alpha) + p_color.greenF() * alpha,
                          c_groundColor.blueF() * (1.0 - alpha) + p_color.blueF() * alpha);
}

} // namespace

QString CommentColorSwatch::builtInColor(const QString &p_token) {
  // Built-in comment-highlight colours. Deliberately translucent so the
  // underlying glyphs stay readable, and anchored to a white page rather than
  // to the app palette.
  //
  // THIS IS THE ONLY TABLE. The theme layer resolves an override first and
  // falls back to here, so the chip and the colour painted on the page cannot
  // disagree.
  static const QHash<QString, QString> c_defaults = {
      {QStringLiteral("yellow"), QStringLiteral("rgba(255, 214, 0, 0.38)")},
      {QStringLiteral("green"), QStringLiteral("rgba(0, 200, 83, 0.32)")},
      {QStringLiteral("blue"), QStringLiteral("rgba(41, 121, 255, 0.30)")},
      {QStringLiteral("pink"), QStringLiteral("rgba(255, 64, 129, 0.30)")},
      {QStringLiteral("purple"), QStringLiteral("rgba(170, 0, 255, 0.28)")}};
  return c_defaults.value(p_token);
}

QColor CommentColorSwatch::parseCssColor(const QString &p_css) {
  const auto css = p_css.trimmed();
  if (css.isEmpty()) {
    return QColor();
  }

  if (css.startsWith(QLatin1Char('#'))) {
    // QColor handles #RGB / #RRGGBB / #AARRGGBB itself. QColor::fromString()
    // does not exist before Qt 6.4, so the constructor is the portable route.
    const QColor color(css);
    return color.isValid() ? color : QColor();
  }

  // Neither QColor nor setNamedColor() accepts functional rgb()/rgba() in any
  // supported Qt, so parse it here. Anchored, or trailing garbage would pass;
  // and the two signatures are SEPARATE alternatives, so `rgb(r,g,b,a)` and
  // `rgba(r,g,b)` are both rejected rather than silently accepted.
  static const QRegularExpression c_functional(
      QStringLiteral(
          R"(^(?:rgb\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*\))"
          R"(|rgba\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d*\.?\d+)\s*\))$)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = c_functional.match(css);
  if (!match.hasMatch()) {
    return QColor();
  }

  const bool hasAlpha = !match.captured(4).isNull();
  const int base = hasAlpha ? 4 : 1;
  const int r = match.captured(base).toInt();
  const int g = match.captured(base + 1).toInt();
  const int b = match.captured(base + 2).toInt();
  if (r > 255 || g > 255 || b > 255) {
    return QColor();
  }

  double alpha = 1.0;
  if (hasAlpha) {
    bool ok = false;
    alpha = match.captured(7).toDouble(&ok);
    if (!ok || alpha < 0.0 || alpha > 1.0) {
      return QColor();
    }
  }

  // The integer components must be normalized before the float API.
  return QColor::fromRgbF(r / 255.0, g / 255.0, b / 255.0, alpha);
}

QIcon CommentColorSwatch::icon(const ColorResolver &p_resolve, const QString &p_token, int p_sizePx,
                               const QString &p_borderCss) {
  if (p_sizePx <= 0) {
    return QIcon();
  }

  // The theme layer only checks that an override is non-empty and '@'-free, so
  // a theme can still hand back an unparseable string. Fall back to this
  // token's built-in, then to the default token's — never to a null/black chip.
  QColor fill;
  if (p_resolve) {
    fill = parseCssColor(p_resolve(p_token));
  }
  if (!fill.isValid()) {
    fill = parseCssColor(builtInColor(p_token));
  }
  if (!fill.isValid()) {
    fill = parseCssColor(builtInColor(CommentColor::defaultToken()));
  }
  if (!fill.isValid()) {
    return QIcon();
  }

  QColor border = parseCssColor(p_borderCss);
  if (!border.isValid()) {
    border = c_fallbackBorder;
  }

  const auto chip = [p_sizePx, &fill, &border](bool p_withBorder) {
    QPixmap pixmap(p_sizePx, p_sizePx);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(compositeOverWhite(fill));
    painter.setPen(p_withBorder ? QPen(border) : QPen(Qt::NoPen));
    if (p_withBorder) {
      // The border is 1px, so inset by one to keep it inside the pixmap.
      painter.drawRect(0, 0, p_sizePx - 1, p_sizePx - 1);
    } else {
      painter.drawRect(0, 0, p_sizePx, p_sizePx);
    }
    return pixmap;
  };

  // TWO STATES, because Qt uses QIcon::On for a CHECKED action.
  //
  // A checkable QAction that also carries an icon gets no checkmark and no
  // radio dot — the icon takes over the indicator column — so every theme marks
  // "checked" with `QMenu::icon:checked { border: 2px solid ... }`. Drawing our
  // own 1px chip border underneath that would stack two rings on the selected
  // row, which reads as a rendering artifact rather than a selection cue.
  //
  // Off (unchecked, and every non-menu caller such as the dock's combo, which
  // never uses On) keeps the chip border, since nothing else delimits a pale
  // swatch against a pale background.
  QIcon icon;
  icon.addPixmap(chip(true), QIcon::Normal, QIcon::Off);
  icon.addPixmap(chip(false), QIcon::Normal, QIcon::On);
  return icon;
}

QIcon CommentColorSwatch::icon(const QString &p_token, int p_sizePx) {
  return icon(ColorResolver(), p_token, p_sizePx);
}
