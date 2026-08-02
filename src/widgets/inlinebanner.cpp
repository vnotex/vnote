#include "inlinebanner.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <utils/widgetutils.h>

#include "propertydefs.h"

using namespace vnotex;

InlineBanner::InlineBanner(QWidget *p_parent) : QFrame(p_parent) {
  setupUI();
  applySeverity();
}

InlineBanner::InlineBanner(Severity p_severity, const QString &p_text, QWidget *p_parent)
    : QFrame(p_parent), m_severity(p_severity) {
  setupUI();
  applySeverity();
  setText(p_text);
}

void InlineBanner::setupUI() {
  // Without this a bare QFrame subclass ignores the background-color coming
  // from the global stylesheet.
  setAttribute(Qt::WA_StyledBackground, true);
  setFrameShape(QFrame::NoFrame);

  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(8, 4, 8, 4);
  m_layout->setSpacing(8);

  m_label = new QLabel(this);
  m_label->setWordWrap(true);
  m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_layout->addWidget(m_label, 1);
}

QString InlineBanner::severityName(Severity p_severity) {
  switch (p_severity) {
  case Severity::Warning:
    return QStringLiteral("warning");
  case Severity::Error:
    return QStringLiteral("error");
  case Severity::Info:
  default:
    return QStringLiteral("info");
  }
}

void InlineBanner::applySeverity() {
  // setPropertyDynamically re-runs unpolish/polish/update, which a live
  // instance needs for the new property value to reach the style engine.
  WidgetUtils::setPropertyDynamically(this, PropertyDefs::c_bannerSeverity,
                                      severityName(m_severity));
}

void InlineBanner::setSeverity(Severity p_severity) {
  if (m_severity == p_severity) {
    return;
  }
  m_severity = p_severity;
  applySeverity();
}

InlineBanner::Severity InlineBanner::getSeverity() const { return m_severity; }

void InlineBanner::setText(const QString &p_text) { m_label->setText(p_text); }

QString InlineBanner::getText() const { return m_label->text(); }

QPushButton *InlineBanner::addActionButton(const QString &p_text) {
  auto *btn = new QPushButton(p_text, this);
  m_layout->addWidget(btn);
  m_actionButtons.append(btn);
  return btn;
}

void InlineBanner::clearActionButtons() {
  for (auto *btn : m_actionButtons) {
    m_layout->removeWidget(btn);
    btn->deleteLater();
  }
  m_actionButtons.clear();
}

QVector<QPushButton *> InlineBanner::getActionButtons() const { return m_actionButtons; }
