#include "newsnippetdialog2.h"

#include <core/servicelocator.h>
#include <core/services/snippetcoreservice.h>
#include <controllers/snippetcontroller.h>

#include "snippetinfowidget2.h"

using namespace vnotex;

NewSnippetDialog2::NewSnippetDialog2(ServiceLocator &p_services, SnippetController *p_controller,
                                     QWidget *p_parent)
    : ScrollDialog(p_parent), m_services(p_services), m_controller(p_controller) {
  setupUI();

  m_infoWidget->setFocus();
}

void NewSnippetDialog2::setupUI() {
  m_infoWidget = new SnippetInfoWidget2(m_services, this);

  // Populate available shortcuts from controller.
  m_infoWidget->populateShortcuts(m_controller->getAvailableShortcuts());

  setCentralWidget(m_infoWidget);

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

  setWindowTitle(tr("New Snippet"));
}

bool NewSnippetDialog2::validateInputs() {
  const auto name = m_infoWidget->getName();
  if (name.isEmpty()) {
    setInformationText(tr("Please specify a name for the snippet."),
                       ScrollDialog::InformationLevel::Error);
    return false;
  }

  auto *snippetService = m_services.get<SnippetCoreService>();
  const auto existing = snippetService->getSnippet(name);
  if (!existing.isEmpty()) {
    setInformationText(tr("Name conflicts with existing snippet."),
                       ScrollDialog::InformationLevel::Error);
    return false;
  }

  setInformationText(QString(), ScrollDialog::InformationLevel::Info);
  return true;
}

void NewSnippetDialog2::acceptedButtonClicked() {
  if (validateInputs() && newSnippet()) {
    accept();
  }
}

bool NewSnippetDialog2::newSnippet() {
  const auto name = m_infoWidget->getName();
  const auto contentJson = m_infoWidget->toContentJson();

  bool ok = m_controller->createSnippet(name, contentJson);
  if (!ok) {
    QString msg = tr("Failed to add snippet (%1).").arg(name);
    qWarning() << msg;
    setInformationText(msg, ScrollDialog::InformationLevel::Error);
    return false;
  }

  // Assign shortcut if selected.
  int shortcut = m_infoWidget->getShortcut();
  if (shortcut >= 0) {
    m_controller->setShortcut(name, shortcut);
  }

  emit snippetCreated(name);
  return true;
}
