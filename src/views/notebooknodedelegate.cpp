#include "notebooknodedelegate.h"

#include <QApplication>
#include <QLineEdit>
#include <QPainter>
#include <QTimer>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <models/notebooknodemodel.h>
#include <utils/iconutils.h>

using namespace vnotex;

NotebookNodeDelegate::NotebookNodeDelegate(ServiceLocator &p_services, QObject *p_parent)
    : QStyledItemDelegate(p_parent), m_services(p_services) {}

NotebookNodeDelegate::~NotebookNodeDelegate() {}

void NotebookNodeDelegate::paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                                 const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return;
  }

  NodeInfo nodeInfo = p_index.data(NotebookNodeModel::NodeInfoRole).value<NodeInfo>();
  if (!nodeInfo.isValid()) {
    QStyledItemDelegate::paint(p_painter, p_option, p_index);
    return;
  }

  paintNode(p_painter, p_option, nodeInfo);
}

void NotebookNodeDelegate::paintNode(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                                     const NodeInfo &p_nodeInfo) const {
  p_painter->save();

  // Get the widget style
  const QWidget *widget = p_option.widget;
  QStyle *style = widget ? widget->style() : QApplication::style();

  // Draw custom node background if specified (before selection)
  QColor bgColor = getNodeBackgroundColor(p_nodeInfo, p_option);
  if (bgColor.isValid()) {
    p_painter->fillRect(p_option.rect, bgColor);
  }

  // Draw selection/hover background on FULL row (respects QSS)
  // PE_PanelItemViewItem only draws background, not text
  style->drawPrimitive(QStyle::PE_PanelItemViewItem, &p_option, p_painter, widget);

  // Calculate icon rect
  QRect iconRect = p_option.rect;
  iconRect.setLeft(iconRect.left() + m_hPadding);
  iconRect.setWidth(m_iconSize);
  iconRect.setTop(iconRect.top() + (p_option.rect.height() - m_iconSize) / 2);
  iconRect.setHeight(m_iconSize);

  // Calculate text rect - leave room for icon on left
  QRect textRect = p_option.rect;
  textRect.setLeft(iconRect.right() + m_hPadding);
  textRect.setRight(p_option.rect.right() - m_hPadding);

  // Draw icon
  QIcon icon = getNodeIcon(p_nodeInfo);
  if (!icon.isNull()) {
    QIcon::Mode iconMode = QIcon::Normal;
    if (!(p_option.state & QStyle::State_Enabled)) {
      iconMode = QIcon::Disabled;
    } else if (p_option.state & QStyle::State_Selected) {
      iconMode = QIcon::Selected;
    }
    icon.paint(p_painter, iconRect, Qt::AlignCenter, iconMode);
  }

  // Prepare text for drawing
  QString text = p_nodeInfo.name;
  QFontMetrics fm(p_option.font);
  QString elidedText = fm.elidedText(text, Qt::ElideRight, textRect.width());

  // Determine text color:
  // 1. Custom color from node visual settings takes priority
  // 2. Otherwise, get color from theme based on selection state
  QColor textColor = getNodeTextColor(p_nodeInfo, p_option);
  if (!textColor.isValid()) {
    // Get text color from theme service based on selection state
    auto *themeService = m_services.get<ThemeService>();
    if (p_option.state & QStyle::State_Selected) {
      // Check active vs inactive window state
      if (p_option.state & QStyle::State_Active) {
        textColor = QColor(themeService->paletteColor(
            QStringLiteral("widgets#qtreeview#item#selected#active#fg")));
      } else {
        textColor = QColor(themeService->paletteColor(
            QStringLiteral("widgets#qtreeview#item#selected#inactive#fg")));
      }
      // Fallback to general selected color if specific one not found
      if (!textColor.isValid()) {
        textColor = QColor(themeService->paletteColor(
            QStringLiteral("widgets#qtreeview#item#selected#fg")));
      }
    } else if (p_option.state & QStyle::State_MouseOver) {
      textColor = QColor(themeService->paletteColor(
          QStringLiteral("widgets#qtreeview#item#hover#fg")));
    }
    // Fallback to normal text color
    if (!textColor.isValid()) {
      textColor = QColor(
          themeService->paletteColor(QStringLiteral("widgets#qtreeview#fg")));
    }
    // Final fallback to palette
    if (!textColor.isValid()) {
      textColor = p_option.palette.text().color();
    }
  }

  // Draw text
  p_painter->setPen(textColor);
  p_painter->setFont(p_option.font);
  p_painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedText);

  // Draw child count badge for containers
  if (m_showChildCount && p_nodeInfo.isFolder) {
    int count = p_nodeInfo.childCount;
    if (count > 0) {
      QString countText = QString("(%1)").arg(count);
      int countWidth = fm.horizontalAdvance(countText);

      QRect countRect = textRect;
      countRect.setLeft(countRect.right() - countWidth - m_hPadding);

      // Use same text color with reduced alpha for badge
      QColor countColor = textColor;
      countColor.setAlpha(150);
      p_painter->setPen(countColor);
      p_painter->drawText(countRect, Qt::AlignRight | Qt::AlignVCenter, countText);
    }
  }

  // Draw focus rect if focused
  if (p_option.state & QStyle::State_HasFocus) {
    QStyleOptionFocusRect focusOpt;
    focusOpt.QStyleOption::operator=(p_option);
    focusOpt.rect = p_option.rect;
    focusOpt.state |= QStyle::State_KeyboardFocusChange;
    style->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOpt, p_painter, widget);
  }

  p_painter->restore();
}

QSize NotebookNodeDelegate::sizeHint(const QStyleOptionViewItem &p_option,
                                     const QModelIndex &p_index) const {
  Q_UNUSED(p_index);

  QFontMetrics fm(p_option.font);
  int height = qMax(fm.height(), m_iconSize) + m_vPadding * 2;

  return QSize(200, height); // Width will be determined by view
}

void NotebookNodeDelegate::setEditorData(QWidget *p_editor, const QModelIndex &p_index) const {
  // Let the base class set the data first
  QStyledItemDelegate::setEditorData(p_editor, p_index);

  // Select only the base name (without extension) for files
  auto *lineEdit = qobject_cast<QLineEdit *>(p_editor);
  if (!lineEdit) {
    return;
  }

  // Check if this is a folder - if so, keep default selectAll behavior
  NodeInfo nodeInfo = p_index.data(NotebookNodeModel::NodeInfoRole).value<NodeInfo>();
  if (nodeInfo.isFolder) {
    return;
  }

  // For files, select only the base name (before the last dot)
  // Use QTimer::singleShot to defer selection until after the view's own
  // selectAll() call that happens after setEditorData returns
  QString text = lineEdit->text();
  int lastDot = text.lastIndexOf(QLatin1Char('.'));
  if (lastDot > 0) {
    QTimer::singleShot(0, lineEdit, [lineEdit, lastDot]() {
      lineEdit->setSelection(0, lastDot);
    });
  }
}

void NotebookNodeDelegate::setShowChildCount(bool p_show) { m_showChildCount = p_show; }

bool NotebookNodeDelegate::showChildCount() const { return m_showChildCount; }

QColor NotebookNodeDelegate::getNodeBackgroundColor(const NodeInfo &p_nodeInfo,
                                                    const QStyleOptionViewItem &p_option) const {
  Q_UNUSED(p_option);

  if (!p_nodeInfo.hasVisualStyle()) {
    return QColor();
  }

  QString colorStr = p_nodeInfo.backgroundColor;
  if (!colorStr.isEmpty()) {
    return QColor(colorStr);
  }

  return QColor();
}

QColor NotebookNodeDelegate::getNodeTextColor(const NodeInfo &p_nodeInfo,
                                              const QStyleOptionViewItem &p_option) const {
  Q_UNUSED(p_option);

  if (!p_nodeInfo.hasVisualStyle()) {
    return QColor();
  }

  QString colorStr = p_nodeInfo.textColor;
  if (!colorStr.isEmpty()) {
    return QColor(colorStr);
  }

  return QColor();
}

QIcon NotebookNodeDelegate::getNodeIcon(const NodeInfo &p_nodeInfo) const {
  auto *themeService = m_services.get<ThemeService>();
  QString iconName;

  if (p_nodeInfo.isFolder) {
    iconName = QStringLiteral("folder_node.svg");
  } else {
    iconName = QStringLiteral("file_node.svg");
  }

  return IconUtils::fetchIcon(themeService->getIconFile(iconName));
}
