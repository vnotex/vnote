#include "vdoublerowitemwidget.h"

#include <QVariant>
#include <QLabel>
#include <QVBoxLayout>

VDoubleRowItemWidget::VDoubleRowItemWidget(QWidget *p_parent)
    : QWidget(p_parent)
{
    m_firstLabel = new QLabel(this);
    m_firstLabel->setProperty("FirstRowLabel", true);

    m_secondLabel = new QLabel(this);
    m_secondLabel->setProperty("SecondRowLabel", true);

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(m_firstLabel);
    layout->addWidget(m_secondLabel);
    layout->addStretch();
    layout->setContentsMargins(3, 0, 0, 0);
    layout->setSpacing(0);

    setLayout(layout);
}

void VDoubleRowItemWidget::setText(const QString &p_firstText,
                                   const QString &p_secondText)
{
    m_firstLabel->setText(p_firstText);

    if (!p_secondText.isEmpty()) {
        m_secondLabel->setText(p_secondText);
        m_secondLabel->show();
    } else {
        m_secondLabel->hide();
    }
}
