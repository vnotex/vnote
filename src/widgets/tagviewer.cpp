#include "tagviewer.h"

#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidgetItem>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <core/vnotex.h>
#include <notebook/node.h>
#include <notebook/notebook.h>
#include <notebook/tagi.h>
#include <utils/iconutils.h>
#include <utils/widgetutils.h>

#include "lineedit.h"
#include "listwidget.h"
#include "mainwindow.h"
#include "messageboxhelper.h"
#include "styleditemdelegate.h"
#include "widgetsfactory.h"

using namespace vnotex;

QIcon TagViewer::s_tagIcon;

QIcon TagViewer::s_selectedTagIcon;

TagViewer::TagViewer(bool p_isPopup, QWidget *p_parent) : QFrame(p_parent), m_isPopup(p_isPopup) {
  initIcons();

  setupUI();
}

void TagViewer::setupUI() {
  auto mainLayout = new QVBoxLayout(this);

  m_searchLineEdit = static_cast<LineEdit *>(WidgetsFactory::createLineEdit(this));
  m_searchLineEdit->setPlaceholderText(tr("Enter to add a tag"));
  m_searchLineEdit->setToolTip(tr("[Shift+Enter] to add current selected tag in the list"));
  connect(m_searchLineEdit, &QLineEdit::textChanged, this, &TagViewer::searchAndFilter);
  connect(m_searchLineEdit, &QLineEdit::returnPressed, this,
          &TagViewer::handleSearchLineEditReturnPressed);
  mainLayout->addWidget(m_searchLineEdit);

  auto tagNameValidator =
      new QRegularExpressionValidator(QRegularExpression("[^>]*"), m_searchLineEdit);
  m_searchLineEdit->setValidator(tagNameValidator);

  setFocusProxy(m_searchLineEdit);
  if (m_isPopup) {
    m_searchLineEdit->installEventFilter(this);
  }

  m_tagList = new ListWidget(this);
  m_tagList->setWrapping(true);
  m_tagList->setFlow(QListView::LeftToRight);
  m_tagList->setIconSize(QSize(18, 18));
  connect(m_tagList, &QListWidget::itemClicked, this, &TagViewer::toggleItemTag);
  connect(m_tagList, &QListWidget::itemActivated, this, &TagViewer::toggleItemTag);
  mainLayout->addWidget(m_tagList);

  if (m_isPopup) {
    m_tagList->installEventFilter(this);
  }
}

bool TagViewer::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (m_isPopup && (p_obj == m_searchLineEdit || p_obj == m_tagList) &&
      p_event->type() == QEvent::KeyPress) {
    auto keyEve = static_cast<QKeyEvent *>(p_event);
    const auto key = keyEve->key();
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
      // Change focus.
      if (p_obj == m_searchLineEdit) {
        m_tagList->setFocus();
      } else {
        m_searchLineEdit->setFocus();
      }
      return true;
    }
  }

  return QFrame::eventFilter(p_obj, p_event);
}

void TagViewer::setNode(Node *p_node) {
  // Since there may be update on tags, always update the list.
  // When first time viewing the tags of one node, it is a good chance to sync the node's tag to DB.
  if (m_node != p_node) {
    m_node = p_node;
    if (m_node) {
      bool ret = tagI()->updateNodeTags(m_node);
      if (!ret) {
        qWarning() << "failed to update tags of node" << m_node->fetchPath();
      }
    }
  }

  m_hasChange = false;

  updateTagList();
}

void TagViewer::updateTagList() {
  m_tagList->clear();
  if (!m_node) {
    return;
  }

  QSet<QString> tagsAdded;
  const auto &nodeTags = m_node->getTags();
  for (const auto &tag : nodeTags) {
    if (tagsAdded.contains(tag)) {
      continue;
    }

    tagsAdded.insert(tag);
    addTagItem(tag, true);
  }

  const auto &allTags = tagI()->getTopLevelTags();
  for (const auto &tag : allTags) {
    addTags(tag, tagsAdded);
  }

  if (!tagsAdded.isEmpty()) {
    m_tagList->setCurrentRow(0);
    // Qt's BUG: need to set it again to make it in grid form after setCurrentRow().
    m_tagList->setWrapping(true);
  }
}

void TagViewer::addTags(const QSharedPointer<Tag> &p_tag, QSet<QString> &p_addedTags) {
  // Itself.
  if (!p_addedTags.contains(p_tag->name())) {
    p_addedTags.insert(p_tag->name());
    addTagItem(p_tag->name(), false);
  }

  // Children.
  for (const auto &child : p_tag->getChildren()) {
    addTags(child, p_addedTags);
  }
}

void TagViewer::initIcons() {
  if (!s_tagIcon.isNull()) {
    return;
  }

  const auto &themeMgr = VNoteX::getInst().getThemeMgr();
  s_tagIcon = IconUtils::fetchIcon(themeMgr.getIconFile(QStringLiteral("tag.svg")));
  s_selectedTagIcon =
      IconUtils::fetchIcon(themeMgr.getIconFile(QStringLiteral("tag_selected.svg")));
}

void TagViewer::addTagItem(const QString &p_tagName, bool p_selected, bool p_prepend) {
  auto item = new QListWidgetItem(p_tagName);
  if (!p_prepend) {
    m_tagList->addItem(item);
  } else {
    m_tagList->insertItem(0, item);
  }

  item->setToolTip(p_tagName);
  item->setData(Qt::UserRole, p_tagName);
  setItemTagSelected(item, p_selected);
}

QString TagViewer::itemTag(const QListWidgetItem *p_item) const {
  return p_item->data(Qt::UserRole).toString();
}

bool TagViewer::isItemTagSelected(const QListWidgetItem *p_item) const {
  return p_item->data(UserRole2).toBool();
}

TagI *TagViewer::tagI() { return m_node->getNotebook()->tag(); }

void TagViewer::searchAndFilter(const QString &p_text) {
  // Take the last tag for search.
  const auto text = p_text.trimmed();

  if (text.isEmpty()) {
    // Show all items.
    filterItems([](const QListWidgetItem *) { return true; });
    return;
  }

  filterItems([this, &text](const QListWidgetItem *p_item) {
    if (itemTag(p_item).contains(text)) {
      return true;
    }
    return false;
  });
}

void TagViewer::filterItems(const std::function<bool(const QListWidgetItem *)> &p_judge) {
  QListWidgetItem *firstHit = nullptr;
  ListWidget::forEachItem(m_tagList, [&firstHit, &p_judge](QListWidgetItem *itemIter) {
    if (p_judge(itemIter)) {
      if (!firstHit) {
        firstHit = itemIter;
      }
      itemIter->setHidden(false);
    } else {
      itemIter->setHidden(true);
    }
    return true;
  });
  m_tagList->setCurrentItem(firstHit);
}

void TagViewer::handleSearchLineEditReturnPressed() {
  if (QGuiApplication::keyboardModifiers() == Qt::ShiftModifier) {
    // Add current selected tag in the list.
    auto item = m_tagList->currentItem();
    if (item && !isItemTagSelected(item)) {
      setItemTagSelected(item, true);
      m_searchLineEdit->clear();
      m_hasChange = true;
    }
  } else {
    // Decode input text and add tags.
    const auto tagName = m_searchLineEdit->text().trimmed();
    if (tagName.isEmpty()) {
      return;
    }

    if (auto item = findItem(tagName)) {
      // Add existing tag.
      setItemTagSelected(item, true);
    } else {
      // Add new tag.
      addTagItem(tagName, true, true);
    }

    m_searchLineEdit->clear();
    m_hasChange = true;
  }
}

void TagViewer::toggleItemTag(QListWidgetItem *p_item) {
  m_hasChange = true;
  setItemTagSelected(p_item, !isItemTagSelected(p_item));
}

void TagViewer::setItemTagSelected(QListWidgetItem *p_item, bool p_selected) {
  p_item->setIcon(p_selected ? s_selectedTagIcon : s_tagIcon);
  p_item->setData(UserRole2, p_selected);
}

QListWidgetItem *TagViewer::findItem(const QString &p_tagName) const {
  return ListWidget::findItem(m_tagList, p_tagName);
}

void TagViewer::save() {
  if (!m_node || !m_hasChange) {
    return;
  }

  QHash<QString, int> selectedTags;
  ListWidget::forEachItem(m_tagList, [this, &selectedTags](QListWidgetItem *itemIter) {
    if (isItemTagSelected(itemIter)) {
      selectedTags.insert(itemTag(itemIter), 0);
    }
    return true;
  });

  if (selectedTags.size() == m_node->getTags().size()) {
    bool same = true;
    for (const auto &tag : m_node->getTags()) {
      auto iter = selectedTags.find(tag);
      if (iter == selectedTags.end()) {
        same = false;
        break;
      } else {
        iter.value()++;
        if (iter.value() > 1) {
          same = false;
          break;
        }
      }
    }

    if (same) {
      return;
    }
  }

  bool ret = tagI()->updateNodeTags(m_node, selectedTags.keys());
  if (ret) {
    VNoteX::getInst().showStatusMessageShort(
        tr("Tags updated: %1").arg(m_node->getTags().join(QLatin1String("; "))));
  } else {
    MessageBoxHelper::notify(MessageBoxHelper::Type::Warning,
                             tr("Failed to update tags of node (%1).").arg(m_node->getName()),
                             VNoteX::getInst().getMainWindow());
  }
}
