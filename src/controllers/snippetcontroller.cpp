#include "snippetcontroller.h"

#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>

using namespace vnotex;

SnippetController::SnippetController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_snippetService(m_services.get<SnippetCoreService>()) {
}

bool SnippetController::createSnippet(const QString &p_name, const QJsonObject &p_contentJson) {
  if (!m_snippetService) {
    emit errorOccurred(tr("Snippet service is not available."));
    return false;
  }

  const bool success = m_snippetService->createSnippet(p_name, p_contentJson);
  if (success) {
    emit snippetListChanged();
  }
  return success;
}

bool SnippetController::deleteSnippet(const QString &p_name) {
  if (!m_snippetService) {
    emit errorOccurred(tr("Snippet service is not available."));
    return false;
  }

  const bool success = m_snippetService->deleteSnippet(p_name);
  if (success) {
    removeShortcut(p_name);
    emit snippetListChanged();
  }
  return success;
}

bool SnippetController::renameSnippet(const QString &p_oldName, const QString &p_newName) {
  if (!m_snippetService) {
    emit errorOccurred(tr("Snippet service is not available."));
    return false;
  }

  const bool success = m_snippetService->renameSnippet(p_oldName, p_newName);
  if (success) {
    const int shortcut = getShortcutForSnippet(p_oldName);
    if (shortcut >= 0) {
      removeShortcut(p_oldName);
      setShortcut(p_newName, shortcut);
    }
    emit snippetListChanged();
  }
  return success;
}

bool SnippetController::updateSnippet(const QString &p_name, const QJsonObject &p_contentJson) {
  if (!m_snippetService) {
    emit errorOccurred(tr("Snippet service is not available."));
    return false;
  }

  return m_snippetService->updateSnippet(p_name, p_contentJson);
}

QJsonObject SnippetController::getSnippet(const QString &p_name) const {
  if (!m_snippetService) {
    return QJsonObject();
  }

  return m_snippetService->getSnippet(p_name);
}

QString SnippetController::getSnippetFolderPath() const {
  if (!m_snippetService) {
    return QString();
  }

  return m_snippetService->getSnippetFolderPath();
}

void SnippetController::setShortcut(const QString &p_name, int p_shortcut) {
  if (p_name.isEmpty() || p_shortcut < 0 || p_shortcut > 99) {
    return;
  }

  const int oldShortcut = getShortcutForSnippet(p_name);
  if (oldShortcut >= 0) {
    m_nameToShortcut.remove(p_name);
    m_shortcutToName.remove(oldShortcut);
  }

  if (m_shortcutToName.contains(p_shortcut)) {
    const QString oldName = m_shortcutToName.value(p_shortcut);
    m_shortcutToName.remove(p_shortcut);
    m_nameToShortcut.remove(oldName);
  }

  m_nameToShortcut.insert(p_name, p_shortcut);
  m_shortcutToName.insert(p_shortcut, p_name);
}

void SnippetController::removeShortcut(const QString &p_name) {
  auto it = m_nameToShortcut.find(p_name);
  if (it == m_nameToShortcut.end()) {
    return;
  }

  const int shortcut = it.value();
  m_nameToShortcut.erase(it);
  m_shortcutToName.remove(shortcut);
}

int SnippetController::getShortcutForSnippet(const QString &p_name) const {
  auto it = m_nameToShortcut.constFind(p_name);
  return it == m_nameToShortcut.constEnd() ? -1 : it.value();
}

QList<int> SnippetController::getAvailableShortcuts() const {
  QList<int> available;
  for (int i = 0; i < 100; ++i) {
    if (!m_shortcutToName.contains(i)) {
      available.append(i);
    }
  }
  return available;
}

QMap<QString, int> SnippetController::getAllShortcuts() const {
  return m_nameToShortcut;
}

void SnippetController::requestApplySnippet(const QString &p_name) {
  emit applySnippetRequested(p_name);
}

void SnippetController::requestNewSnippet() {
  emit newSnippetRequested();
}

void SnippetController::requestDeleteSnippet(const QString &p_name) {
  emit deleteSnippetRequested(p_name);
}

void SnippetController::requestProperties(const QString &p_name) {
  emit propertiesRequested(p_name);
}
