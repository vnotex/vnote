#include "notebookselector2.h"

#include <QAbstractItemView>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QScrollBar>
#include <QStyledItemDelegate>

#include <core/servicelocator.h>
#include <core/services/notebookservice.h>
#include <utils/iconutils.h>
#include <utils/widgetutils.h>
#include <core/configmgr2.h>
#include <core/sessionconfig.h>

using namespace vnotex;
using vnotex::core::NotebookService;

NotebookSelector2::NotebookSelector2(ServiceLocator &p_services, QWidget *p_parent)
    : ComboBox(p_parent),
      NavigationMode(NavigationMode::Type::StagedDoubleKeys, this),
      m_services(p_services) {
  auto itemDelegate = new QStyledItemDelegate(this);
  setItemDelegate(itemDelegate);

  view()->installEventFilter(this);

  setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

  // Save current notebook selection when user activates a notebook.
  connect(this, QOverload<int>::of(&QComboBox::activated), this,
          &NotebookSelector2::saveCurrentNotebook);
}

void NotebookSelector2::loadNotebooks() {
  clear();

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService) {
    qWarning() << "NotebookSelector2: NotebookService not available";
    return;
  }

  QJsonArray notebooks = notebookService->listNotebooks();
  sortNotebooks(notebooks);

  for (const auto &nb : notebooks) {
    addNotebookItem(nb.toObject());
  }

  updateGeometry();

  // Restore previously selected notebook.
  restoreCurrentNotebook();
}

void NotebookSelector2::sortNotebooks(QJsonArray &p_notebooks) const {
  // Convert to list for sorting.
  QList<QJsonObject> nbList;
  for (const auto &nb : p_notebooks) {
    nbList.append(nb.toObject());
  }

  bool reversed = false;
  switch (m_viewOrder) {
  case ViewOrder::OrderedByNameReversed:
    reversed = true;
    Q_FALLTHROUGH();
  case ViewOrder::OrderedByName:
    std::sort(nbList.begin(), nbList.end(),
              [reversed](const QJsonObject &p_a, const QJsonObject &p_b) {
                QString nameA = p_a.value("name").toString().toLower();
                QString nameB = p_b.value("name").toString().toLower();
                if (reversed) {
                  return nameB < nameA;
                } else {
                  return nameA < nameB;
                }
              });
    break;

  case ViewOrder::OrderedByCreatedTimeReversed:
    reversed = true;
    Q_FALLTHROUGH();
  case ViewOrder::OrderedByCreatedTime:
    std::sort(nbList.begin(), nbList.end(),
              [reversed](const QJsonObject &p_a, const QJsonObject &p_b) {
                qint64 timeA = p_a.value("created_time").toVariant().toLongLong();
                qint64 timeB = p_b.value("created_time").toVariant().toLongLong();
                if (reversed) {
                  return timeB < timeA;
                } else {
                  return timeA < timeB;
                }
              });
    break;

  default:
    break;
  }

  // Convert back to QJsonArray.
  p_notebooks = QJsonArray();
  for (const auto &nb : nbList) {
    p_notebooks.append(nb);
  }
}


void NotebookSelector2::addNotebookItem(const QJsonObject &p_notebookJson) {
  QString name = p_notebookJson.value("name").toString();
  QString rootPath = p_notebookJson.value("rootFolder").toString();
  QString description = p_notebookJson.value("description").toString();
  QString guid = p_notebookJson.value("id").toString();

  // Icon from JSON if available, otherwise generate.
  QIcon icon;
  QString iconPath = p_notebookJson.value("icon").toString();
  if (!iconPath.isEmpty()) {
    icon = QIcon(iconPath);
  }

  int idx = count();
  addItem(generateItemIcon(name, icon), name);
  setItemToolTip(idx, generateItemToolTip(name, rootPath, description));
  // Store GUID string for identification and save/restore.
  setItemData(idx, guid, NotebookGuidRole);
}

void NotebookSelector2::fetchIconColor(const QString &p_name, QString &p_fg, QString &p_bg) {
  static QVector<QString> backgroundColors = {
      "#80558c", "#df7861", "#f65a83", "#3b9ae1", "#277bc0", "#42855b", "#a62349", "#a66cff",
      "#9c9efe", "#54bab9", "#79b4b7", "#57cc99", "#916bbf", "#5c7aea", "#6867ac",
  };

  int hashVal = 0;
  for (int i = 0; i < p_name.size(); ++i) {
    hashVal += p_name[i].unicode();
  }

  p_fg = "#ffffff";
  p_bg = backgroundColors[hashVal % backgroundColors.size()];
}

QIcon NotebookSelector2::generateItemIcon(const QString &p_name, const QIcon &p_customIcon) {
  if (!p_customIcon.isNull()) {
    return p_customIcon;
  }

  if (p_name.isEmpty()) {
    return QIcon();
  }

  QString fg, bg;
  fetchIconColor(p_name, fg, bg);
  return IconUtils::drawTextRectIcon(p_name.at(0).toUpper(), fg, bg, "", 50, 58);
}

QString NotebookSelector2::generateItemToolTip(const QString &p_name, const QString &p_rootPath,
                                               const QString &p_description) {
  return tr("Notebook: %1\nRoot folder: %2\nDescription: %3")
      .arg(p_name, p_rootPath, p_description);
}

QString NotebookSelector2::getItemToolTip(int p_idx) const {
  return itemData(p_idx, Qt::ToolTipRole).toString();
}

void NotebookSelector2::setItemToolTip(int p_idx, const QString &p_tooltip) {
  setItemData(p_idx, p_tooltip, Qt::ToolTipRole);
}

void NotebookSelector2::setCurrentNotebook(const QString &p_guid) {
  int idx = findNotebook(p_guid);
  if (idx >= 0) {
    setCurrentIndex(idx);
    setToolTip(getItemToolTip(idx));
  }
}

QString NotebookSelector2::currentNotebookId() const {
  int idx = currentIndex();
  if (idx >= 0) {
    return itemData(idx, NotebookGuidRole).toString();
  }
  return QString();
}

int NotebookSelector2::findNotebook(const QString &p_guid) const {
  for (int i = 0; i < count(); ++i) {
    if (itemData(i, NotebookGuidRole).toString() == p_guid) {
      return i;
    }
  }
  return -1;
}

QVector<void *> NotebookSelector2::getVisibleNavigationItems() {
  QVector<void *> items;
  auto listView = dynamic_cast<QListView *>(view());
  if (listView) {
    m_navigationIndexes = WidgetUtils::getVisibleIndexes(listView);
    for (auto &index : m_navigationIndexes) {
      items.push_back(&index);
    }
  }
  return items;
}

void NotebookSelector2::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) {
  if (p_idx == -1) {
    // Major.
    p_label->move(rect().topRight() - QPoint(p_label->width() + 2, 2));
  } else {
    // Second.
    // Reparent it to the list view.
    auto listView = view();
    p_label->setParent(listView);

    auto index = *static_cast<QModelIndex *>(p_item);

    int extraWidth = p_label->width() + 2;
    auto vbar = listView->verticalScrollBar();
    if (vbar && vbar->minimum() != vbar->maximum()) {
      extraWidth += vbar->width();
    }

    const auto rt = listView->visualRect(index);
    const int x = rt.x() + view()->width() - extraWidth;
    const int y = rt.y();
    p_label->move(x, y);
  }
}

void NotebookSelector2::handleTargetHit(void *p_item) {
  if (!p_item) {
    setFocus();
    showPopup();
  } else {
    hidePopup();
    auto index = *static_cast<QModelIndex *>(p_item);
    setCurrentIndex(index.row());
    emit activated(index.row());
  }
}

bool NotebookSelector2::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (p_event->type() == QEvent::KeyPress && p_obj == view()) {
    if (WidgetUtils::processKeyEventLikeVi(view(), static_cast<QKeyEvent *>(p_event))) {
      return true;
    }
  }
  return ComboBox::eventFilter(p_obj, p_event);
}

void NotebookSelector2::clearNavigation() {
  m_navigationIndexes.clear();

  NavigationMode::clearNavigation();
}

void NotebookSelector2::mousePressEvent(QMouseEvent *p_event) {
  // Only when notebooks are loaded and there is no notebook, we could prompt for new notebook.
  if (count() == 0) {
    emit newNotebookRequested();
    return;
  }

  ComboBox::mousePressEvent(p_event);
}

void NotebookSelector2::setViewOrder(int p_order) {
  if (m_viewOrder == p_order) {
    return;
  }

  if (p_order >= 0 && p_order < ViewOrder::ViewOrderMax) {
    m_viewOrder = static_cast<ViewOrder>(p_order);
    loadNotebooks();
  }
}

void NotebookSelector2::saveCurrentNotebook() {
  int idx = currentIndex();
  if (idx < 0) {
    return;
  }

  // Get GUID from item data.
  QString guid = itemData(idx, NotebookGuidRole).toString();
  if (!guid.isEmpty()) {
    auto *configMgr = m_services.get<ConfigMgr2>();
    if (configMgr) {
      configMgr->getSessionConfig().setCurrentNotebook(guid);
    }
  }
}

void NotebookSelector2::restoreCurrentNotebook() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }

  const QString &guid = configMgr->getSessionConfig().getCurrentNotebook();
  if (guid.isEmpty()) {
    return;
  }

  int idx = findNotebook(guid);
  if (idx >= 0) {
    setCurrentIndex(idx);
    setToolTip(getItemToolTip(idx));
  }
}

