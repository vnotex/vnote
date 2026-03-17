#include "listwidget.h"

#include <QApplication>
#include <QKeyEvent>
#include <QTimer>

#include "styleditemdelegate.h"
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <utils/widgetutils.h>

using namespace vnotex;

ListWidget::ListWidget(QWidget *p_parent) : ListWidget(false, p_parent) {}

ListWidget::ListWidget(bool p_enhancedStyle, QWidget *p_parent) : QListWidget(p_parent) {
  m_clickTimer = new QTimer(this);
  m_clickTimer->setSingleShot(true);
  connect(m_clickTimer, &QTimer::timeout, this, &ListWidget::handleItemClick);

  connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    if (m_isDoubleClick && m_clickedItem == item) {
      // There will be a single click right after double click.
      m_clickTimer->stop();
      handleItemClick();
      return;
    }

    m_isDoubleClick = false;
    m_clickedItem = item;
    m_clickTimer->start(QApplication::doubleClickInterval());
  });

  connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
    m_clickTimer->stop();
    m_isDoubleClick = true;
    m_clickedItem = item;
  });

  if (p_enhancedStyle) {
    auto delegate = new StyledItemDelegate(
        QSharedPointer<StyledItemDelegateListWidget>::create(this), StyledItemDelegate::None,
        QBrush(), QBrush(), this);
    setItemDelegate(delegate);
  }
}

ListWidget::ListWidget(ServiceLocator &p_services, bool p_enhancedStyle, QWidget *p_parent)
    : QListWidget(p_parent) {
  m_clickTimer = new QTimer(this);
  m_clickTimer->setSingleShot(true);
  connect(m_clickTimer, &QTimer::timeout, this, &ListWidget::handleItemClick);

  connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    if (m_isDoubleClick && m_clickedItem == item) {
      // There will be a single click right after double click.
      m_clickTimer->stop();
      handleItemClick();
      return;
    }

    m_isDoubleClick = false;
    m_clickedItem = item;
    m_clickTimer->start(QApplication::doubleClickInterval());
  });

  connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
    m_clickTimer->stop();
    m_isDoubleClick = true;
    m_clickedItem = item;
  });

  if (p_enhancedStyle) {
    QBrush highlightFg, highlightBg;
    auto *themeService = p_services.get<ThemeService>();
    if (themeService) {
      highlightFg = QColor(
          themeService->paletteColor(QStringLiteral("widgets#styleditemdelegate#highlight#fg")));
      highlightBg = QColor(
          themeService->paletteColor(QStringLiteral("widgets#styleditemdelegate#highlight#bg")));
    }
    auto delegate = new StyledItemDelegate(
        QSharedPointer<StyledItemDelegateListWidget>::create(this), StyledItemDelegate::None,
        highlightFg, highlightBg, this);
    setItemDelegate(delegate);
  }
}

void ListWidget::keyPressEvent(QKeyEvent *p_event) {
  if (WidgetUtils::processKeyEventLikeVi(this, p_event)) {
    return;
  }

  bool activateItem = false;
  const int key = p_event->key();
  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    activateItem = true;
  }
  // On Mac OS X, it is `Command+O` to activate an item, instead of Return.
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  if (key == Qt::Key_O && p_event->modifiers() == Qt::ControlModifier) {
    activateItem = true;
  }
#endif

  if (activateItem) {
    if (auto item = currentItem()) {
      emit itemActivated(item);
      emit itemActivatedPlus(item, ActivateReason::Button);
    }
    return;
  }

  QListWidget::keyPressEvent(p_event);
}

QVector<QListWidgetItem *> ListWidget::getVisibleItems(const QListWidget *p_widget) {
  QVector<QListWidgetItem *> items;

  auto firstItem = p_widget->itemAt(0, 0);
  if (!firstItem) {
    return items;
  }

  auto lastItem = p_widget->itemAt(p_widget->viewport()->rect().bottomLeft());

  int firstRow = p_widget->row(firstItem);
  int lastRow = lastItem ? p_widget->row(lastItem) : (p_widget->count() - 1);
  for (int i = firstRow; i <= lastRow; ++i) {
    auto item = p_widget->item(i);
    if (!item->isHidden() && item->flags() != Qt::NoItemFlags) {
      items.append(item);
    }
  }

  return items;
}

// LEGACY: Separator colors previously came from ThemeService::inst() singleton.
// Now returns items without themed colors. Will be fully removed when
// HistoryPanel is migrated to DI.
QListWidgetItem *ListWidget::createSeparatorItem(const QString &p_text) {
  QListWidgetItem *item = new QListWidgetItem(p_text, nullptr, ItemTypeSeparator);
  item->setFlags(Qt::NoItemFlags);
  return item;
}

bool ListWidget::isSeparatorItem(const QListWidgetItem *p_item) {
  return p_item->type() == ItemTypeSeparator;
}

QListWidgetItem *ListWidget::findItem(const QListWidget *p_widget, const QVariant &p_data) {
  QListWidgetItem *item = nullptr;
  forEachItem(p_widget, [&item, &p_data](QListWidgetItem *itemIter) {
    if (itemIter->data(Qt::UserRole) == p_data) {
      item = itemIter;
      return false;
    }

    return true;
  });

  return item;
}

void ListWidget::forEachItem(const QListWidget *p_widget,
                             const std::function<bool(QListWidgetItem *p_item)> &p_func) {
  int cnt = p_widget->count();
  for (int i = 0; i < cnt; ++i) {
    if (!p_func(p_widget->item(i))) {
      return;
    }
  }
}

void ListWidget::handleItemClick() {
  if (m_clickedItem) {
    emit itemActivatedPlus(m_clickedItem, m_isDoubleClick ? ActivateReason::DoubleClick
                                                          : ActivateReason::SingleClick);
    m_isDoubleClick = false;
    m_clickedItem = nullptr;
  }
}
