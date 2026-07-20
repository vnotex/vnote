#include "encodingbutton.h"

#include <QAction>
#include <QByteArray>
#include <QList>
#include <QMenu>
#include <QSet>
#include <QStringList>
#include <QTextCodec>

using namespace vnotex;

EncodingButton::EncodingButton(QWidget *p_parent)
    : QToolButton(p_parent), m_currentEncoding(QStringLiteral("UTF-8")) {
  setPopupMode(QToolButton::InstantPopup);
  setToolButtonStyle(Qt::ToolButtonTextOnly);
  setAutoRaise(true);
  // Drop the dropdown arrow to match the editor's status-bar buttons (e.g.
  // the "Spelling" indicator), which are plain text triggers.
  setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
  setToolTip(tr("Reinterpret the file with a different encoding"));

  // Defer the (relatively expensive) codec enumeration off the window-open
  // path: the menu is populated lazily the first time it is shown.
  auto *menu = new QMenu(this);
  connect(menu, &QMenu::aboutToShow, this, &EncodingButton::buildMenu);
  setMenu(menu);

  setText(m_currentEncoding);
}

void EncodingButton::buildMenu() {
  if (m_menuBuilt) {
    return;
  }
  m_menuBuilt = true;

  QMenu *menu = this->menu();
  if (!menu) {
    return;
  }

  // Curated shortlist of common encodings.
  const QStringList shortlist = {
      QStringLiteral("UTF-8"),   QStringLiteral("GB18030"),  QStringLiteral("GBK"),
      QStringLiteral("Big5"),    QStringLiteral("UTF-16LE"), QStringLiteral("UTF-16BE"),
      QStringLiteral("ISO-8859-1"), QStringLiteral("Shift-JIS")};

  auto addEntry = [this, menu](const QString &p_name) {
    QAction *act = menu->addAction(p_name);
    connect(act, &QAction::triggered, this,
            [this, p_name]() { emit encodingChangeRequested(p_name); });
  };

  QSet<QString> seen;
  for (const QString &name : shortlist) {
    // Only offer codecs actually available on this platform build.
    if (QTextCodec::codecForName(name.toUtf8())) {
      addEntry(name);
      seen.insert(name);
    }
  }

  menu->addSeparator();

  // Full available list (sorted, de-duplicated against the shortlist).
  QStringList all;
  const QList<QByteArray> codecs = QTextCodec::availableCodecs();
  for (const QByteArray &c : codecs) {
    const QString name = QString::fromUtf8(c);
    if (!seen.contains(name)) {
      all.append(name);
      seen.insert(name);
    }
  }
  all.sort(Qt::CaseInsensitive);
  for (const QString &name : all) {
    addEntry(name);
  }
}

void EncodingButton::setCurrentEncoding(const QString &p_codecName) {
  m_currentEncoding = p_codecName.isEmpty() ? QStringLiteral("UTF-8") : p_codecName;
  setText(m_currentEncoding);
}
