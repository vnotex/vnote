#include "taskentrydelegate.h"

#include <QApplication>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>

using namespace vnotex;

TaskEntryDelegate::TaskEntryDelegate(ServiceLocator &p_services, QObject *p_parent)
    : QStyledItemDelegate(p_parent), m_services(p_services) {}

QFont TaskEntryDelegate::pathFont(const QFont &p_baseFont) {
  QFont font = p_baseFont;
  const int pointSize = font.pointSize();
  if (pointSize > 2) {
    font.setPointSize(pointSize - 1);
  } else if (font.pixelSize() > 2) {
    // Fonts configured by pixel size report pointSize() == -1.
    font.setPixelSize(font.pixelSize() - 1);
  }
  return font;
}

QColor TaskEntryDelegate::resolveTextColor(const QStyleOptionViewItem &p_option) const {
  QColor color;

  if (auto *themeService = m_services.get<ThemeService>()) {
    if (p_option.state & QStyle::State_Selected) {
      if (p_option.state & QStyle::State_Active) {
        color = QColor(themeService->paletteColor(
            QStringLiteral("widgets#qtreeview#item#selected#active#fg")));
      } else {
        color = QColor(themeService->paletteColor(
            QStringLiteral("widgets#qtreeview#item#selected#inactive#fg")));
      }
      if (!color.isValid()) {
        color = QColor(
            themeService->paletteColor(QStringLiteral("widgets#qtreeview#item#selected#fg")));
      }
    } else if (p_option.state & QStyle::State_MouseOver) {
      color =
          QColor(themeService->paletteColor(QStringLiteral("widgets#qtreeview#item#hover#fg")));
    }

    if (!color.isValid()) {
      color = QColor(themeService->paletteColor(QStringLiteral("widgets#qtreeview#fg")));
    }
  }

  if (!color.isValid()) {
    color = p_option.palette.text().color();
  }

  return color;
}

void TaskEntryDelegate::paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                              const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return;
  }

  const QString path = p_index.data(PathRole).toString();
  if (path.isEmpty()) {
    QStyledItemDelegate::paint(p_painter, p_option, p_index);
    return;
  }

  p_painter->save();

  const QWidget *widget = p_option.widget;
  QStyle *style = widget ? widget->style() : QApplication::style();

  // Selection / hover background on the full row (respects QSS).
  style->drawPrimitive(QStyle::PE_PanelItemViewItem, &p_option, p_painter, widget);

  QRect contentRect = p_option.rect;
  contentRect.setLeft(contentRect.left() + m_hPadding);
  contentRect.setRight(contentRect.right() - m_hPadding);

  // Icon on the left, vertically centred across both lines.
  const QIcon icon = p_index.data(Qt::DecorationRole).value<QIcon>();
  if (!icon.isNull()) {
    const QRect iconRect(contentRect.left(),
                         contentRect.top() + (contentRect.height() - m_iconSize) / 2, m_iconSize,
                         m_iconSize);
    QIcon::Mode iconMode = QIcon::Normal;
    if (!(p_option.state & QStyle::State_Enabled)) {
      iconMode = QIcon::Disabled;
    } else if (p_option.state & QStyle::State_Selected) {
      iconMode = QIcon::Selected;
    }
    icon.paint(p_painter, iconRect, Qt::AlignCenter, iconMode);
    contentRect.setLeft(iconRect.right() + m_hPadding);
  }

  const QColor textColor = resolveTextColor(p_option);
  QColor dimColor = textColor;
  // The selected row's background reduces contrast, so dim less there.
  dimColor.setAlpha((p_option.state & QStyle::State_Selected) ? 200 : 150);

  const QFont nameFont = p_option.font;
  const QFont smallFont = pathFont(nameFont);
  const QFontMetrics nameFm(nameFont);
  const QFontMetrics smallFm(smallFont);

  // Line 1: the task label.
  QRect nameRect(contentRect.left(), contentRect.top() + m_vPadding, contentRect.width(),
                 nameFm.height());
  p_painter->setFont(nameFont);
  p_painter->setPen(textColor);
  p_painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      nameFm.elidedText(p_index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                        nameRect.width()));

  // Line 2: the task path, smaller and dimmed.
  QRect pathRect(contentRect.left(), nameRect.bottom() + m_lineSpacing, contentRect.width(),
                 smallFm.height());
  p_painter->setFont(smallFont);
  p_painter->setPen(dimColor);
  p_painter->drawText(pathRect, Qt::AlignLeft | Qt::AlignVCenter,
                      smallFm.elidedText(path, Qt::ElideMiddle, pathRect.width()));

  if (p_option.state & QStyle::State_HasFocus) {
    QStyleOptionFocusRect focusOpt;
    focusOpt.QStyleOption::operator=(p_option);
    focusOpt.rect = p_option.rect;
    focusOpt.state |= QStyle::State_KeyboardFocusChange;
    style->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOpt, p_painter, widget);
  }

  p_painter->restore();
}

QSize TaskEntryDelegate::sizeHint(const QStyleOptionViewItem &p_option,
                                  const QModelIndex &p_index) const {
  const QFontMetrics nameFm(p_option.font);

  const QString path = p_index.data(PathRole).toString();
  if (path.isEmpty()) {
    return QSize(200, qMax(nameFm.height(), m_iconSize) + m_vPadding * 2);
  }

  const QFontMetrics smallFm(pathFont(p_option.font));
  const int textHeight = nameFm.height() + m_lineSpacing + smallFm.height();

  // Width is determined by the view.
  return QSize(200, qMax(textHeight, m_iconSize) + m_vPadding * 2);
}
