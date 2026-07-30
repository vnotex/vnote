#include "mainwindowtaskcontext.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QWidget>

using namespace vnotex;

MainWindowTaskContext::MainWindowTaskContext(Providers p_providers, QWidget *p_promptParent)
    : m_providers(std::move(p_providers)), m_promptParent(p_promptParent) {}

QString MainWindowTaskContext::call(const StringProvider &p_provider) {
  return p_provider ? p_provider() : QString();
}

QString MainWindowTaskContext::currentNotebookId() const {
  return call(m_providers.currentNotebookId);
}

QString MainWindowTaskContext::currentBufferPath() const {
  return call(m_providers.currentBufferPath);
}

QString MainWindowTaskContext::currentBufferNotebookId() const {
  return call(m_providers.currentBufferNotebookId);
}

QString MainWindowTaskContext::currentBufferRelativePath() const {
  return call(m_providers.currentBufferRelativePath);
}

QString MainWindowTaskContext::selectedText() const { return call(m_providers.selectedText); }

QString MainWindowTaskContext::promptString(const QString &p_title, const QString &p_label,
                                            const QString &p_default, bool p_password,
                                            bool &p_cancelled) const {
  bool ok = false;
  const auto mode = p_password ? QLineEdit::Password : QLineEdit::Normal;
  const auto result =
      QInputDialog::getText(m_promptParent, p_title, p_label, mode, p_default, &ok);
  p_cancelled = !ok;
  return ok ? result : QString();
}
