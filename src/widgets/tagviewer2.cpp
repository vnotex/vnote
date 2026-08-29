#include "tagviewer2.h"

#include <QDebug>
#include <QEvent>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

#include <core/global.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

#include <vxcore/notebook_json_keys.h>

#include "listwidget.h"

using namespace vnotex;

TagViewer2::TagViewer2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  initIcons();
  setupUI();

  auto *themeService = m_services.get<ThemeService>();
  if (themeService) {
    connect(themeService, &ThemeService::themeChanged, this, &TagViewer2::refreshIcons);
  }
}

void TagViewer2::initIcons() {
  auto *themeService = m_services.get<ThemeService>();
  if (themeService) {
    m_tagIcon = IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("tag.svg")));
    m_selectedTagIcon =
        IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("tag_selected.svg")));
  }
}

void TagViewer2::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  m_searchEdit = new QLineEdit(this);
  m_searchEdit->setPlaceholderText(tr("Search tags..."));
  m_searchEdit->setClearButtonEnabled(true);
  connect(m_searchEdit, &QLineEdit::textChanged, this, &TagViewer2::filterTags);
  connect(m_searchEdit, &QLineEdit::returnPressed, this, &TagViewer2::handleReturnPressed);
  mainLayout->addWidget(m_searchEdit);

  setFocusProxy(m_searchEdit);

  m_tagList = new ListWidget(this);
  m_tagList->setWrapping(true);
  m_tagList->setFlow(QListView::LeftToRight);
  m_tagList->setIconSize(QSize(18, 18));
  connect(m_tagList, &QListWidget::itemClicked, this, &TagViewer2::toggleItemTag);
  // itemActivated is deliberately NOT connected: on styles where
  // SH_ItemView_ActivateItemOnSingleClick is set, Qt emits it alongside
  // itemClicked, and a double-click emits it on top of two itemClicked. The
  // tri-state cycle is not self-inverse (Partial -> All -> None), so a
  // double-fire would take a mixed tag straight into removedTags() and untag it
  // from every file that had it. Return is handled explicitly below instead.
  m_tagList->installEventFilter(this);
  mainLayout->addWidget(m_tagList);
}

void TagViewer2::setNodeId(const NodeIdentifier &p_nodeId) { setNodeIds({p_nodeId}); }

void TagViewer2::setNodeIds(const QList<NodeIdentifier> &p_nodeIds) {
  m_nodeIds.clear();
  for (const auto &id : p_nodeIds) {
    if (id.isValid()) {
      m_nodeIds.append(id);
    }
  }

  m_tagStates.clear();
  m_originalStates.clear();
  m_tagCounts.clear();
  m_readableNodeIds.clear();
  updateTagList();
}

int TagViewer2::unreadableTargetCount() const {
  return m_nodeIds.size() - m_readableNodeIds.size();
}

QString TagViewer2::notebookId() const {
  return m_nodeIds.isEmpty() ? QString() : m_nodeIds.first().notebookId;
}

void TagViewer2::updateTagList() {
  m_tagList->clear();

  if (m_nodeIds.isEmpty()) {
    return;
  }

  // Count, per tag, how many targets carry it.
  //
  // NotebookCoreService::getFileInfo returns an EMPTY object on failure, which is
  // indistinguishable from "this file has no tags" — so the error out-param is
  // mandatory here. Counting an unreadable file as untagged would show a tag that
  // every file actually carries as Partial, and two clicks on it (Partial -> All
  // -> None) would then untag it from every file that legitimately had it.
  // Unreadable targets are therefore excluded from BOTH the counts and the
  // denominator, and reported via unreadableTargetCount() so the dialog can warn.
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  QStringList orderedFileTags;
  if (notebookSvc) {
    for (const auto &id : m_nodeIds) {
      VxCoreError err = VXCORE_OK;
      auto fileInfo = notebookSvc->getFileInfo(id.notebookId, id.relativePath, &err);
      if (err != VXCORE_OK) {
        continue;
      }
      m_readableNodeIds.append(id);
      const auto tagsArray = fileInfo.value(QLatin1String(vxcore::kJsonKeyTags)).toArray();
      QSet<QString> seenForThisFile;
      for (const auto &tagVal : tagsArray) {
        const auto tagName = tagVal.toString();
        if (tagName.isEmpty() || seenForThisFile.contains(tagName)) {
          continue;
        }
        seenForThisFile.insert(tagName);
        if (!m_tagCounts.contains(tagName)) {
          orderedFileTags.append(tagName);
        }
        m_tagCounts[tagName] = m_tagCounts.value(tagName) + 1;
      }
    }
  }

  const int targetCount = m_readableNodeIds.size();

  // All first, then Partial.
  QSet<QString> addedNames;
  for (int pass = 0; pass < 2; ++pass) {
    const TagState wanted = (pass == 0) ? TagState::All : TagState::Partial;
    for (const auto &tagName : orderedFileTags) {
      const int count = m_tagCounts.value(tagName);
      const TagState state = (count >= targetCount) ? TagState::All : TagState::Partial;
      if (state != wanted || addedNames.contains(tagName)) {
        continue;
      }
      addedNames.insert(tagName);
      m_originalStates.insert(tagName, state);
      addTagItem(tagName, state);
    }
  }

  // Then the remaining notebook tags, all None.
  auto *tagSvc = m_services.get<TagService>();
  if (tagSvc) {
    const auto allTags = tagSvc->listTags(notebookId());
    for (const auto &tagVal : allTags) {
      const auto tagObj = tagVal.toObject();
      const auto tagName = tagObj.value(QStringLiteral("name")).toString();
      if (tagName.isEmpty() || addedNames.contains(tagName)) {
        continue;
      }
      addedNames.insert(tagName);
      m_originalStates.insert(tagName, TagState::None);
      addTagItem(tagName, TagState::None);
    }
  }

  if (!addedNames.isEmpty()) {
    m_tagList->setCurrentRow(0);
    // Qt BUG workaround: reset wrapping after setCurrentRow().
    m_tagList->setWrapping(true);
  }
}

void TagViewer2::addTagItem(const QString &p_tagName, TagState p_state, bool p_prepend) {
  auto *item = new QListWidgetItem(p_tagName);
  if (!p_prepend) {
    m_tagList->addItem(item);
  } else {
    m_tagList->insertItem(0, item);
  }

  item->setData(Qt::UserRole, p_tagName);
  setItemTagState(item, p_state);
}

QString TagViewer2::itemTag(const QListWidgetItem *p_item) const {
  return p_item->data(Qt::UserRole).toString();
}

TagViewer2::TagState TagViewer2::itemTagState(const QListWidgetItem *p_item) const {
  return static_cast<TagState>(p_item->data(UserRole2).toInt());
}

QString TagViewer2::tooltipForState(const QString &p_tagName, TagState p_state) const {
  if (p_state == TagState::Partial) {
    return tr("Applied to %1 of %2 selected files")
        .arg(m_tagCounts.value(p_tagName))
        .arg(m_readableNodeIds.size());
  }
  return p_tagName;
}

void TagViewer2::setItemTagState(QListWidgetItem *p_item, TagState p_state) {
  p_item->setIcon(p_state == TagState::All ? m_selectedTagIcon : m_tagIcon);
  p_item->setData(UserRole2, static_cast<int>(p_state));

  QFont f = p_item->font();
  f.setItalic(p_state == TagState::Partial);
  p_item->setFont(f);

  const auto tag = itemTag(p_item);
  p_item->setToolTip(tooltipForState(tag, p_state));
  m_tagStates[tag] = p_state;
}

bool TagViewer2::eventFilter(QObject *p_obj, QEvent *p_event) {
  // Keyboard activation of the current tag. This replaces the itemActivated
  // connection (see setupUI) so exactly ONE toggle happens per gesture.
  if (p_obj == m_tagList && p_event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(p_event);
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
      if (auto *item = m_tagList->currentItem()) {
        toggleItemTag(item);
      }
      return true;
    }
  }
  return QFrame::eventFilter(p_obj, p_event);
}

void TagViewer2::toggleItemTag(
    QListWidgetItem *p_item) { // Partial -> All, All -> None, None -> All.
  const TagState next = (itemTagState(p_item) == TagState::All) ? TagState::None : TagState::All;
  setItemTagState(p_item, next);
}

QSet<QString> TagViewer2::addedTags() const {
  QSet<QString> result;
  for (auto it = m_tagStates.constBegin(); it != m_tagStates.constEnd(); ++it) {
    if (it.value() == TagState::All &&
        m_originalStates.value(it.key(), TagState::None) != TagState::All) {
      result.insert(it.key());
    }
  }
  return result;
}

QSet<QString> TagViewer2::removedTags() const {
  QSet<QString> result;
  for (auto it = m_tagStates.constBegin(); it != m_tagStates.constEnd(); ++it) {
    if (it.value() == TagState::None &&
        m_originalStates.value(it.key(), TagState::None) != TagState::None) {
      result.insert(it.key());
    }
  }
  return result;
}

QListWidgetItem *TagViewer2::findItem(const QString &p_tagName) const {
  return ListWidget::findItem(m_tagList, p_tagName);
}

void TagViewer2::filterTags(const QString &p_text) {
  auto text = p_text.trimmed();

  if (text.isEmpty()) {
    ListWidget::forEachItem(m_tagList, [](QListWidgetItem *p_item) {
      p_item->setHidden(false);
      return true;
    });
    return;
  }

  QListWidgetItem *firstHit = nullptr;
  ListWidget::forEachItem(m_tagList, [&text, &firstHit](QListWidgetItem *p_item) {
    if (p_item->text().contains(text, Qt::CaseInsensitive)) {
      p_item->setHidden(false);
      if (!firstHit) {
        firstHit = p_item;
      }
    } else {
      p_item->setHidden(true);
    }
    return true;
  });
  m_tagList->setCurrentItem(firstHit);
}

void TagViewer2::handleReturnPressed() {
  auto tagName = m_searchEdit->text().trimmed();
  if (tagName.isEmpty()) {
    return;
  }

  // Determine the display name (leaf segment for paths, full name otherwise).
  const bool isPath = tagName.contains(QLatin1Char('/'));
  const auto leafName = isPath ? tagName.section(QLatin1Char('/'), -1) : tagName;

  if (leafName.isEmpty()) {
    m_searchEdit->clear();
    return;
  }

  // Check if the leaf tag already exists in the list.
  if (auto *item = findItem(isPath ? leafName : tagName)) {
    setItemTagState(item, TagState::All);
  } else {
    auto *tagSvc = m_services.get<TagService>();
    if (tagSvc) {
      if (isPath) {
        tagSvc->createTagPath(notebookId(), tagName);
      } else {
        tagSvc->createTag(notebookId(), tagName);
      }
    }
    addTagItem(leafName, TagState::All, true);
  }

  m_searchEdit->clear();
}

bool TagViewer2::save() {
  if (m_nodeIds.isEmpty()) {
    return true;
  }

  // LEGACY single-node overwrite path (TagPopup2 only). It writes the whole tag
  // array of ONE file and collapses Partial to "not selected", so it must never
  // be reached from a batch selection — the dialog path uses addedTags() /
  // removedTags() and a per-id delta apply instead.
  if (m_nodeIds.size() != 1) {
    qWarning() << "TagViewer2::save() is the single-node legacy path; a multi-node "
                  "selection must use addedTags()/removedTags()";
    return false;
  }

  const auto &nodeId = m_nodeIds.first();

  QSet<QString> selected;
  for (auto it = m_tagStates.constBegin(); it != m_tagStates.constEnd(); ++it) {
    if (it.value() == TagState::All) {
      selected.insert(it.key());
    }
  }

  QSet<QString> original;
  for (auto it = m_originalStates.constBegin(); it != m_originalStates.constEnd(); ++it) {
    if (it.value() == TagState::All) {
      original.insert(it.key());
    }
  }

  if (selected == original) {
    return true;
  }

  // Build JSON array from selected tags.
  QJsonArray tagsArray;
  for (const auto &tag : selected) {
    tagsArray.append(tag);
  }
  auto tagsJson = QString::fromUtf8(QJsonDocument(tagsArray).toJson(QJsonDocument::Compact));

  auto *tagSvc = m_services.get<TagService>();
  if (!tagSvc) {
    return false;
  }

  return tagSvc->updateFileTags(nodeId.notebookId, nodeId.relativePath, tagsJson);
}

bool TagViewer2::isTagSupported() const {
  if (m_nodeIds.isEmpty()) {
    return false;
  }

  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return false;
  }

  auto config = notebookSvc->getNotebookConfig(notebookId());
  return config.value(QStringLiteral("type")).toString() == QStringLiteral("bundled");
}

void TagViewer2::refreshIcons() {
  initIcons();
  // Re-apply icons to existing tag list items.
  for (int i = 0; i < m_tagList->count(); ++i) {
    auto *item = m_tagList->item(i);
    item->setIcon(itemTagState(item) == TagState::All ? m_selectedTagIcon : m_tagIcon);
  }
}
