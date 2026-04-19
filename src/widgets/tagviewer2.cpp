#include "tagviewer2.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
  connect(m_tagList, &QListWidget::itemActivated, this, &TagViewer2::toggleItemTag);
  mainLayout->addWidget(m_tagList);
}

void TagViewer2::setNodeId(const NodeIdentifier &p_nodeId) {
  m_nodeId = p_nodeId;
  m_selectedTags.clear();
  m_originalTags.clear();
  updateTagList();
}

void TagViewer2::updateTagList() {
  m_tagList->clear();

  if (!m_nodeId.isValid()) {
    return;
  }

  // Get file's current tags.
  QSet<QString> fileTags;
  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (notebookSvc) {
    auto fileInfo = notebookSvc->getFileInfo(m_nodeId.notebookId, m_nodeId.relativePath);
    auto tagsArray = fileInfo.value(QStringLiteral("tags")).toArray();
    for (const auto &tagVal : tagsArray) {
      fileTags.insert(tagVal.toString());
    }
  }

  m_originalTags = fileTags;
  m_selectedTags = fileTags;

  // Add file's tags first (selected).
  QSet<QString> addedTags;
  for (const auto &tag : fileTags) {
    if (!addedTags.contains(tag)) {
      addedTags.insert(tag);
      addTagItem(tag, true);
    }
  }

  // Get all notebook tags.
  auto *tagSvc = m_services.get<TagService>();
  if (tagSvc) {
    auto allTags = tagSvc->listTags(m_nodeId.notebookId);
    for (const auto &tagVal : allTags) {
      auto tagObj = tagVal.toObject();
      auto tagName = tagObj.value(QStringLiteral("name")).toString();
      if (!tagName.isEmpty() && !addedTags.contains(tagName)) {
        addedTags.insert(tagName);
        addTagItem(tagName, false);
      }
    }
  }

  if (!addedTags.isEmpty()) {
    m_tagList->setCurrentRow(0);
    // Qt BUG workaround: reset wrapping after setCurrentRow().
    m_tagList->setWrapping(true);
  }
}

void TagViewer2::addTagItem(const QString &p_tagName, bool p_selected, bool p_prepend) {
  auto *item = new QListWidgetItem(p_tagName);
  if (!p_prepend) {
    m_tagList->addItem(item);
  } else {
    m_tagList->insertItem(0, item);
  }

  item->setToolTip(p_tagName);
  item->setData(Qt::UserRole, p_tagName);
  setItemTagSelected(item, p_selected);
}

QString TagViewer2::itemTag(const QListWidgetItem *p_item) const {
  return p_item->data(Qt::UserRole).toString();
}

bool TagViewer2::isItemTagSelected(const QListWidgetItem *p_item) const {
  return p_item->data(UserRole2).toBool();
}

void TagViewer2::setItemTagSelected(QListWidgetItem *p_item, bool p_selected) {
  p_item->setIcon(p_selected ? m_selectedTagIcon : m_tagIcon);
  p_item->setData(UserRole2, p_selected);

  auto tag = itemTag(p_item);
  if (p_selected) {
    m_selectedTags.insert(tag);
  } else {
    m_selectedTags.remove(tag);
  }
}

void TagViewer2::toggleItemTag(QListWidgetItem *p_item) {
  setItemTagSelected(p_item, !isItemTagSelected(p_item));
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
    setItemTagSelected(item, true);
  } else {
    auto *tagSvc = m_services.get<TagService>();
    if (tagSvc) {
      if (isPath) {
        tagSvc->createTagPath(m_nodeId.notebookId, tagName);
      } else {
        tagSvc->createTag(m_nodeId.notebookId, tagName);
      }
    }
    addTagItem(leafName, true, true);
  }

  m_searchEdit->clear();
}

bool TagViewer2::save() {
  if (!m_nodeId.isValid()) {
    return true;
  }

  if (m_selectedTags == m_originalTags) {
    return true;
  }

  // Build JSON array from selected tags.
  QJsonArray tagsArray;
  for (const auto &tag : m_selectedTags) {
    tagsArray.append(tag);
  }
  auto tagsJson = QString::fromUtf8(QJsonDocument(tagsArray).toJson(QJsonDocument::Compact));

  auto *tagSvc = m_services.get<TagService>();
  if (!tagSvc) {
    return false;
  }

  return tagSvc->updateFileTags(m_nodeId.notebookId, m_nodeId.relativePath, tagsJson);
}

bool TagViewer2::isTagSupported() const {
  if (!m_nodeId.isValid()) {
    return false;
  }

  auto *notebookSvc = m_services.get<NotebookCoreService>();
  if (!notebookSvc) {
    return false;
  }

  auto config = notebookSvc->getNotebookConfig(m_nodeId.notebookId);
  return config.value(QStringLiteral("type")).toString() == QStringLiteral("bundled");
}

void TagViewer2::refreshIcons() {
  initIcons();
  // Re-apply icons to existing tag list items.
  for (int i = 0; i < m_tagList->count(); ++i) {
    auto *item = m_tagList->item(i);
    if (isItemTagSelected(item)) {
      item->setIcon(m_selectedTagIcon);
    } else {
      item->setIcon(m_tagIcon);
    }
  }
}
