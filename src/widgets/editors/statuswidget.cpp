#include "statuswidget.h"

#include <QDebug>
#include <QLabel>
#include <QStackedLayout>
#include <QTimer>

using namespace vnotex;

StatusWidget::StatusWidget(QWidget *p_parent) : QWidget(p_parent) {
  m_mainLayout = new QStackedLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);

  m_messageLabel = new QLabel(this);
  m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_mainLayout->addWidget(m_messageLabel);

  m_messageTimer = new QTimer(this);
  m_messageTimer->setSingleShot(true);
  connect(m_messageTimer, &QTimer::timeout, this, &StatusWidget::clearMessage);
}

StatusWidget::~StatusWidget() {
  if (m_editorWidget) {
    m_editorWidget->setParent(nullptr);
  }
}

void StatusWidget::showMessage(const QString &p_msg, int p_milliseconds) {
  if (p_msg.isEmpty()) {
    clearMessage();
    return;
  }

  m_messageLabel->setText(p_msg);
  m_mainLayout->setCurrentWidget(m_messageLabel);

  if (p_milliseconds > 0) {
    m_messageTimer->start(p_milliseconds);
  }
}

void StatusWidget::setEditorStatusWidget(const QSharedPointer<QWidget> &p_editorWidget) {
  Q_ASSERT(!m_editorWidget);
  m_editorWidget = p_editorWidget;
  m_mainLayout->addWidget(m_editorWidget.data());
  m_mainLayout->setCurrentWidget(m_editorWidget.data());
}

void StatusWidget::resizeEvent(QResizeEvent *p_event) {
  QWidget::resizeEvent(p_event);

  int maxWidth = width() - 10;
  if (maxWidth <= 0) {
    maxWidth = width();
  }
  m_messageLabel->setMaximumWidth(maxWidth);
}

void StatusWidget::clearMessage() {
  m_messageLabel->clear();
  if (m_editorWidget) {
    m_mainLayout->setCurrentWidget(m_editorWidget.data());
  }
}
