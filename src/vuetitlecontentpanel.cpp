#include "vuetitlecontentpanel.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QVariant>
#include <QString>

VUETitleContentPanel::VUETitleContentPanel(QWidget *p_contentWidget,
                                           QWidget *p_parent)
    : QWidget(p_parent)
{
    m_titleLabel = new QLabel(this);
    m_titleLabel->setProperty("TitleLabel", true);

    p_contentWidget->setParent(this);

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(m_titleLabel);
    layout->addWidget(p_contentWidget);

    layout->setContentsMargins(0, 0, 0, 0);

    setLayout(layout);
}

void VUETitleContentPanel::setTitle(const QString &p_title)
{
    m_titleLabel->setText(p_title);
}

void VUETitleContentPanel::clearTitle()
{
    m_titleLabel->clear();
}
