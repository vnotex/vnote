#include "snippetpropertiesdialog2.h"

#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>
#include <controllers/snippetcontroller.h>

#include "snippetinfowidget2.h"

using namespace vnotex;

SnippetPropertiesDialog2::SnippetPropertiesDialog2(ServiceLocator &p_services,
                                                   SnippetController *p_controller,
                                                   const QString &p_snippetName,
                                                   QWidget *p_parent)
    : ScrollDialog(p_parent),
      m_services(p_services),
      m_controller(p_controller),
      m_originalName(p_snippetName) {
  setupUI();

  m_infoWidget->setFocus();
}

void SnippetPropertiesDialog2::setupUI() {
  // Fetch snippet data from service.
  auto *snippetService = m_services.get<SnippetCoreService>();
  const auto snippetData = snippetService->getSnippet(m_originalName);

  // Store original shortcut.
  m_originalShortcut = m_controller->getShortcutForSnippet(m_originalName);

  // Create info widget in edit mode (pre-filled).
  m_infoWidget = new SnippetInfoWidget2(m_services, snippetData, this);

  // Populate shortcuts with available ones plus the current one.
  m_infoWidget->populateShortcuts(m_controller->getAvailableShortcuts(), m_originalShortcut);

  setCentralWidget(m_infoWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  // If snippet is builtin, make it read-only (AFTER button box exists).
  if (snippetData.value(QStringLiteral("isBuiltin")).toBool()) {
    m_infoWidget->setReadOnly(true);
    setButtonEnabled(QDialogButtonBox::Ok, false);
  }

  setWindowTitle(tr("%1 Properties").arg(m_originalName));
}

bool SnippetPropertiesDialog2::validateInputs() {
  const auto name = m_infoWidget->getName();
  if (name.isEmpty()) {
    setInformationText(tr("Please specify a name for the snippet."),
                       ScrollDialog::InformationLevel::Error);
    return false;
  }

  // Check name collision only if name changed (case-insensitive).
  if (name.toLower() != m_originalName.toLower()) {
    auto *snippetService = m_services.get<SnippetCoreService>();
    const auto existing = snippetService->getSnippet(name);
    if (!existing.isEmpty()) {
      setInformationText(tr("Name conflicts with existing snippet."),
                         ScrollDialog::InformationLevel::Error);
      return false;
    }
  }

  setInformationText(QString(), ScrollDialog::InformationLevel::Info);
  return true;
}

void SnippetPropertiesDialog2::acceptedButtonClicked() {
  if (validateInputs() && saveSnippetProperties()) {
    accept();
  }
}

bool SnippetPropertiesDialog2::saveSnippetProperties() {
  const auto newName = m_infoWidget->getName();
  const auto contentJson = m_infoWidget->toContentJson();

  // Update snippet content.
  bool ok = m_controller->updateSnippet(m_originalName, contentJson);
  if (!ok) {
    QString msg = tr("Failed to update snippet (%1).").arg(m_originalName);
    qWarning() << msg;
    setInformationText(msg, ScrollDialog::InformationLevel::Error);
    return false;
  }

  // Track the current name (may change after rename).
  QString finalName = m_originalName;

  // Rename if name changed (case-insensitive compare).
  if (newName.toLower() != m_originalName.toLower()) {
    ok = m_controller->renameSnippet(m_originalName, newName);
    if (!ok) {
      QString msg = tr("Failed to rename snippet from %1 to %2.").arg(m_originalName, newName);
      qWarning() << msg;
      setInformationText(msg, ScrollDialog::InformationLevel::Error);
      return false;
    }
    finalName = newName;
  }

  // Handle shortcut changes.
  int newShortcut = m_infoWidget->getShortcut();
  if (newShortcut != m_originalShortcut) {
    if (m_originalShortcut >= 0 && newShortcut < 0) {
      // Shortcut removed.
      m_controller->removeShortcut(finalName);
    } else if (newShortcut >= 0) {
      // Shortcut added or changed.
      m_controller->setShortcut(finalName, newShortcut);
    }
  }

  emit snippetUpdated(finalName);
  return true;
}
