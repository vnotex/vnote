#include "consoleviewer.h"

#include <QPlainTextEdit>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <widgets/titlebar.h>

using namespace vnotex;

namespace {
// Cap the console at a bounded number of blocks to avoid unbounded growth on
// long-running or chatty tasks.
const int c_maxBlockCount = 5000;
} // namespace

ConsoleViewer::ConsoleViewer(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI();
}

void ConsoleViewer::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  setupTitleBar();
  mainLayout->addWidget(m_titleBar);

  m_output = new QPlainTextEdit(this);
  m_output->setReadOnly(true);
  m_output->setMaximumBlockCount(c_maxBlockCount);
  m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
  mainLayout->addWidget(m_output, 1);

  setFocusProxy(m_output);
}

void ConsoleViewer::setupTitleBar() {
  m_titleBar =
      new TitleBar(m_services.get<ThemeService>(), QString(), false, TitleBar::Action::None, this);
  m_titleBar->setActionButtonsAlwaysShown(true);

  auto *clearBtn = m_titleBar->addActionButton(QStringLiteral("clear.svg"), tr("Clear"));
  connect(clearBtn, &QToolButton::clicked, this, &ConsoleViewer::clear);
}

void ConsoleViewer::appendOutput(const QString &p_text) {
  if (p_text.isEmpty()) {
    return;
  }

  // Insert at the end without adding an extra newline (task output already
  // includes its own framing/newlines).
  auto cursor = m_output->textCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(p_text);

  auto *scrollBar = m_output->verticalScrollBar();
  scrollBar->setValue(scrollBar->maximum());
}

void ConsoleViewer::clear() { m_output->clear(); }
