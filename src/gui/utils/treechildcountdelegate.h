#ifndef TREECHILDCOUNTDELEGATE_H
#define TREECHILDCOUNTDELEGATE_H

#include <QApplication>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTreeView>

#include "treeviewutils.h"

namespace vnotex {

// Header-only so isolated view targets need no additional GUI linkage. No new
// signals/properties: the inherited QStyledItemDelegate meta-object is sufficient.
class TreeChildCountDelegate : public QStyledItemDelegate {
public:
  // Parent to the view so horizontal scroll-copying invalidates the fixed badges.
  explicit TreeChildCountDelegate(QObject *p_parent = nullptr, int p_countRole = -1)
      : QStyledItemDelegate(p_parent), m_countRole(p_countRole) {
    if (auto *view = qobject_cast<QTreeView *>(p_parent)) {
      connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, this,
              [view]() { view->viewport()->update(); });
    }
  }

  void setShowChildCount(bool p_show) {
    if (m_showChildCount == p_show) {
      return;
    }
    m_showChildCount = p_show;
    if (auto *view = qobject_cast<QTreeView *>(parent())) {
      view->doItemsLayout();
      view->viewport()->update();
    }
  }

  bool showChildCount() const { return m_showChildCount; }

  void paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
             const QModelIndex &p_index) const override {
    const int count = countForIndex(p_index);
    if (!m_showChildCount || count <= 0) {
      QStyledItemDelegate::paint(p_painter, p_option, p_index);
      return;
    }
    QStyleOptionViewItem opt(p_option);
    initStyleOption(&opt, p_index);
    const auto badge = childCountBadge(opt, count);
    if (badge.m_rect.isEmpty()) {
      QStyledItemDelegate::paint(p_painter, p_option, p_index);
      return;
    }

    auto *style = opt.widget ? opt.widget->style() : QApplication::style();
    const int boundary = badge.m_rect.left() - badge.m_gap;
    const QRect textRect = ItemViewUtils::textRect(opt, style);
    opt.text = opt.fontMetrics.elidedText(opt.text, opt.textElideMode,
                                          qMax(0, boundary - textRect.left()));

    // Disjoint clips keep the full-row style/selection/focus geometry without
    // double-compositing translucent backgrounds or painting icons under badges.
    const QRect row = viewportRow(opt);
    p_painter->save();
    p_painter->setClipRect(row.adjusted(0, 0, boundary - row.right() - 1, 0), Qt::IntersectClip);
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p_painter, opt.widget);
    p_painter->restore();

    opt.text.clear();
    opt.icon = QIcon();
    opt.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration |
                      QStyleOptionViewItem::HasCheckIndicator);
    p_painter->save();
    p_painter->setClipRect(row.adjusted(boundary - row.left(), 0, 0, 0), Qt::IntersectClip);
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p_painter, opt.widget);
    p_painter->restore();

    paintChildCount(p_painter, badge, opt);
  }

  QSize sizeHint(const QStyleOptionViewItem &p_option, const QModelIndex &p_index) const override {
    QStyleOptionViewItem opt(p_option);
    initStyleOption(&opt, p_index);
    QSize size = QStyledItemDelegate::sizeHint(p_option, p_index);
    const int count = countForIndex(p_index);
    if (m_showChildCount && count > 0) {
      // Standard text chrome and the viewport-fixed badge can have different
      // right insets. Reserve the difference as well as the badge and its gap.
      opt.rect = QRect(QPoint(), size);
      auto *style = opt.widget ? opt.widget->style() : QApplication::style();
      const int textRightInset = opt.rect.right() - ItemViewUtils::textRect(opt, style).right();
      const auto spacing = TreeViewUtils::horizontalSpacing(opt, 8);
      size.rwidth() += childCountWidth(opt, count) + qMax(0, spacing.m_right - textRightInset);
    }
    return size;
  }

protected:
  struct ChildCountBadge {
    QString m_text;
    QFont m_font;
    QRect m_rect;
    int m_gap = 0;
  };

  // Custom delegates supply their cached count rather than forcing lazy models
  // to fetch children during painting. Zero/unknown counts produce no badge.
  ChildCountBadge childCountBadge(const QStyleOptionViewItem &p_option, int p_count) const {
    ChildCountBadge badge;
    if (!m_showChildCount || p_count <= 0 || p_option.rect.height() <= 2) {
      return badge;
    }
    badge.m_text = QString::number(p_count);
    badge.m_font = badgeFont(p_option.font);
    const QFontMetrics fm(badge.m_font);
    const auto spacing = TreeViewUtils::horizontalSpacing(p_option, 8);
    badge.m_gap = spacing.m_gap;
    const int height = qMin(fm.height() + 4, p_option.rect.height() - 2);
    const int width = qMax(height, fm.horizontalAdvance(badge.m_text) + 8);
    const QRect row = viewportRow(p_option);
    badge.m_rect = QRect(row.right() - spacing.m_right - width + 1,
                         row.top() + (row.height() - height) / 2, width, height);
    return badge;
  }

  int childCountWidth(const QStyleOptionViewItem &p_option, int p_count) const {
    if (!m_showChildCount || p_count <= 0) {
      return 0;
    }
    const QFontMetrics fm(badgeFont(p_option.font));
    return qMax(fm.height() + 4, fm.horizontalAdvance(QString::number(p_count)) + 8) +
           TreeViewUtils::horizontalSpacing(p_option, 8).m_gap;
  }

  static void paintChildCount(QPainter *p_painter, const ChildCountBadge &p_badge,
                              const QStyleOptionViewItem &p_option) {
    if (p_badge.m_rect.isEmpty()) {
      return;
    }
    p_painter->save();
    p_painter->setRenderHint(QPainter::Antialiasing);
    // Ancillary counts keep the theme's normal foreground, including over QSS
    // selections which do not use the system HighlightedText/background pair.
    const auto group = !(p_option.state & QStyle::State_Enabled) ? QPalette::Disabled
                       : p_option.state & QStyle::State_Active   ? QPalette::Active
                                                                 : QPalette::Inactive;
    const QColor foreground = p_option.palette.color(group, QPalette::Text);
    QColor fill = foreground;
    fill.setAlphaF(fill.alphaF() * 0.12);
    p_painter->setPen(Qt::NoPen);
    p_painter->setBrush(fill);
    const qreal radius = p_badge.m_rect.height() / 2.0;
    p_painter->drawRoundedRect(QRectF(p_badge.m_rect), radius, radius);
    QColor text = foreground;
    text.setAlphaF(text.alphaF() * 0.85);
    p_painter->setPen(text);
    p_painter->setFont(p_badge.m_font);
    p_painter->drawText(p_badge.m_rect, Qt::AlignCenter, p_badge.m_text);
    p_painter->restore();
  }

private:
  int countForIndex(const QModelIndex &p_index) const {
    if (!p_index.isValid()) {
      return 0;
    }
    return m_countRole < 0 ? p_index.model()->rowCount(p_index) : p_index.data(m_countRole).toInt();
  }

  static QRect viewportRow(const QStyleOptionViewItem &p_option) {
    QRect row(p_option.rect);
    if (const auto *view = qobject_cast<const QTreeView *>(p_option.widget)) {
      row.setLeft(0);
      row.setRight(view->viewport()->rect().right());
    }
    return row;
  }

  static QFont badgeFont(QFont p_font) {
    if (p_font.pointSizeF() > 0) {
      p_font.setPointSizeF(p_font.pointSizeF() * 0.85);
    } else {
      p_font.setPixelSize(qMax(1, qRound(p_font.pixelSize() * 0.85)));
    }
    return p_font;
  }

  const int m_countRole;
  bool m_showChildCount = true;
};

} // namespace vnotex

#endif // TREECHILDCOUNTDELEGATE_H
