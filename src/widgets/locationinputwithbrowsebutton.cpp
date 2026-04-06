#include "locationinputwithbrowsebutton.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

#include <widgets/widgetsfactory.h>

using namespace vnotex;

QString LocationInputWithBrowseButton::s_lastBrowsePath;

LocationInputWithBrowseButton::LocationInputWithBrowseButton(QWidget *p_parent)
    : QWidget(p_parent) {
  auto layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_lineEdit = WidgetsFactory::createLineEdit(this);
  layout->addWidget(m_lineEdit, 1);
  connect(m_lineEdit, &QLineEdit::textChanged, this, &LocationInputWithBrowseButton::textChanged);

  auto browseBtn = new QPushButton(tr("Browse"), this);
  layout->addWidget(browseBtn);
  connect(browseBtn, &QPushButton::clicked, this, &LocationInputWithBrowseButton::clicked);

  // Fix vertical alignment in QFormLayout - prevent vertical expansion.
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QString LocationInputWithBrowseButton::text() const { return m_lineEdit->text(); }

void LocationInputWithBrowseButton::setText(const QString &p_text) {
  m_lineEdit->setText(p_text);
  if (!p_text.isEmpty()) {
    QFileInfo fi(p_text);
    auto dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (!dir.isEmpty()) {
      s_lastBrowsePath = dir;
    }
  }
}

QString LocationInputWithBrowseButton::toolTip() const { return m_lineEdit->toolTip(); }

void LocationInputWithBrowseButton::setToolTip(const QString &p_tip) {
  m_lineEdit->setToolTip(p_tip);
}

void LocationInputWithBrowseButton::setPlaceholderText(const QString &p_text) {
  m_lineEdit->setPlaceholderText(p_text);
}

QString LocationInputWithBrowseButton::defaultBrowsePath() {
  return s_lastBrowsePath.isEmpty() ? QDir::homePath() : s_lastBrowsePath;
}
