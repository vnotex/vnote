#include "tagservice.h"

#include <QtGlobal>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

TagService::TagService(VxCoreContextHandle p_context, HookManager *p_hookMgr, QObject *p_parent)
    : TagCoreService(p_context, p_parent), m_hookMgr(p_hookMgr) {
  Q_ASSERT(m_hookMgr);
}

TagService::~TagService() {
}

QObject *TagService::asQObject() {
  return this;
}

// ============ Tag Lifecycle (with hooks) ============

bool TagService::createTag(const QString &p_notebookId, const QString &p_tagName) {
  TagOperationEvent event;
  event.notebookId = p_notebookId;
  event.tagName = p_tagName;
  event.operation = QStringLiteral("create");
  if (m_hookMgr->doAction(HookNames::TagBeforeCreate, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = TagCoreService::createTag(p_notebookId, p_tagName);

  if (ok) {
    m_hookMgr->doAction(HookNames::TagAfterCreate, event);
  }
  return ok;
}

bool TagService::deleteTag(const QString &p_notebookId, const QString &p_tagName) {
  TagOperationEvent event;
  event.notebookId = p_notebookId;
  event.tagName = p_tagName;
  event.operation = QStringLiteral("delete");
  if (m_hookMgr->doAction(HookNames::TagBeforeDelete, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = TagCoreService::deleteTag(p_notebookId, p_tagName);

  if (ok) {
    m_hookMgr->doAction(HookNames::TagAfterDelete, event);
  }
  return ok;
}

bool TagService::moveTag(const QString &p_notebookId, const QString &p_tagName,
                         const QString &p_parentTag) {
  TagOperationEvent event;
  event.notebookId = p_notebookId;
  event.tagName = p_tagName;
  event.parentTag = p_parentTag;
  event.operation = QStringLiteral("move");
  if (m_hookMgr->doAction(HookNames::TagBeforeMove, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = TagCoreService::moveTag(p_notebookId, p_tagName, p_parentTag);

  if (ok) {
    m_hookMgr->doAction(HookNames::TagAfterMove, event);
  }
  return ok;
}

// ============ File-Tag Operations (with hooks) ============

bool TagService::tagFile(const QString &p_notebookId, const QString &p_filePath,
                         const QString &p_tagName) {
  FileTagEvent event;
  event.notebookId = p_notebookId;
  event.filePath = p_filePath;
  event.tagName = p_tagName;
  event.operation = QStringLiteral("tag");
  if (m_hookMgr->doAction(HookNames::FileBeforeTag, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = TagCoreService::tagFile(p_notebookId, p_filePath, p_tagName);

  if (ok) {
    m_hookMgr->doAction(HookNames::FileAfterTag, event);
  }
  return ok;
}

bool TagService::untagFile(const QString &p_notebookId, const QString &p_filePath,
                           const QString &p_tagName) {
  FileTagEvent event;
  event.notebookId = p_notebookId;
  event.filePath = p_filePath;
  event.tagName = p_tagName;
  event.operation = QStringLiteral("untag");
  if (m_hookMgr->doAction(HookNames::FileBeforeUntag, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = TagCoreService::untagFile(p_notebookId, p_filePath, p_tagName);

  if (ok) {
    m_hookMgr->doAction(HookNames::FileAfterUntag, event);
  }
  return ok;
}

bool TagService::updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                                const QString &p_tagsJson) {
  FileTagEvent event;
  event.notebookId = p_notebookId;
  event.filePath = p_filePath;
  event.tagsJson = p_tagsJson;
  event.operation = QStringLiteral("update_tags");
  if (m_hookMgr->doAction(HookNames::FileBeforeTag, event)) {
    return false; // Cancelled by plugin.
  }

  bool ok = TagCoreService::updateFileTags(p_notebookId, p_filePath, p_tagsJson);

  if (ok) {
    m_hookMgr->doAction(HookNames::FileAfterTag, event);
  }
  return ok;
}

// ============ Pass-through (no hooks) ============

bool TagService::createTagPath(const QString &p_notebookId, const QString &p_tagPath) {
  return TagCoreService::createTagPath(p_notebookId, p_tagPath);
}

QJsonArray TagService::listTags(const QString &p_notebookId) const {
  return TagCoreService::listTags(p_notebookId);
}

QJsonArray TagService::searchByTags(const QString &p_notebookId, const QStringList &p_tags) {
  return TagCoreService::searchByTags(p_notebookId, p_tags);
}

QJsonArray TagService::findFilesByTags(const QString &p_notebookId, const QStringList &p_tags,
                                       const QString &p_op) {
  return TagCoreService::findFilesByTags(p_notebookId, p_tags, p_op);
}
