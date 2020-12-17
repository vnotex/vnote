#include "statuswidget.h"

#include <QTimer>
#include <QLabel>
#include <QHBoxLayout>

using namespace vnotex;

StatusWidget::StatusWidget(QWidget *p_parent)
    : QWidget(p_parent)
{
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_messageLabel);

    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, &QTimer::timeout,
            m_messageLabel, &QLabel::clear);
}

StatusWidget::~StatusWidget()
{
    if (m_editorWidget) {
        m_editorWidget->setParent(nullptr);
    }
}

void StatusWidget::showMessage(const QString &p_msg, int p_milliseconds)
{
    m_messageLabel->setText(p_msg);
    m_messageTimer->start(p_milliseconds);
}

void StatusWidget::setEditorStatusWidget(const QSharedPointer<QWidget> &p_editorWidget)
{
    Q_ASSERT(!m_editorWidget);
    m_editorWidget = p_editorWidget;
    layout()->addWidget(m_editorWidget.data());
}
