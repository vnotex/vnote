#ifndef ITEMPROXYSTYLE_H
#define ITEMPROXYSTYLE_H

#include <QProxyStyle>

class QStyleOptionViewItem;
class QTextOption;

namespace vnotex {
// Draw item with text segments highlighted.
class ItemProxyStyle : public QProxyStyle {
  Q_OBJECT
public:
  explicit ItemProxyStyle(QStyle *p_style = nullptr);

  void drawControl(QStyle::ControlElement p_element, const QStyleOption *p_option,
                   QPainter *p_painter, const QWidget *p_widget = nullptr) const Q_DECL_OVERRIDE;

private:
  bool drawItemViewItem(const QStyleOption *p_option, QPainter *p_painter,
                        const QWidget *p_widget) const;

  void viewItemDrawText(QPainter *p_painter, const QStyleOptionViewItem *p_option,
                        const QRect &p_rect) const;

  QString calculateElidedText(const QString &text, const QTextOption &textOption, const QFont &font,
                              const QRect &textRect, const Qt::Alignment valign,
                              Qt::TextElideMode textElideMode, int flags,
                              bool lastVisibleLineShouldBeElided,
                              QPointF *paintStartPosition) const;
};
} // namespace vnotex

#endif // ITEMPROXYSTYLE_H
