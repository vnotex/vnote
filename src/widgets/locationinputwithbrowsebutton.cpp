#include "locationinputwithbrowsebutton.h"

#include <QPushButton>
#include <QLineEdit>
#include <QHBoxLayout>

#include <widgets/widgetsfactory.h>

using namespace vnotex;

LocationInputWithBrowseButton::LocationInputWithBrowseButton(QWidget *p_parent)
    : QWidget(p_parent)
{
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_lineEdit = WidgetsFactory::createLineEdit(this);
    layout->addWidget(m_lineEdit, 1);
    connect(m_lineEdit, &QLineEdit::textChanged,
            this, &LocationInputWithBrowseButton::textChanged);

    auto browseBtn = new QPushButton(tr("Browse"), this);
    layout->addWidget(browseBtn);
    connect(browseBtn, &QPushButton::clicked,
            this, &LocationInputWithBrowseButton::clicked);
}

QString LocationInputWithBrowseButton::text() const
{
    return m_lineEdit->text();
}

void LocationInputWithBrowseButton::setText(const QString &p_text)
{
    m_lineEdit->setText(p_text);
}

QString LocationInputWithBrowseButton::toolTip() const
{
    return m_lineEdit->toolTip();
}

void LocationInputWithBrowseButton::setToolTip(const QString &p_tip)
{
    m_lineEdit->setToolTip(p_tip);
}

void LocationInputWithBrowseButton::setPlaceholderText(const QString &p_text)
{
    m_lineEdit->setPlaceholderText(p_text);
}
