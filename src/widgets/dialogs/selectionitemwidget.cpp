#include "selectionitemwidget.h"

#include <QCheckBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>

#include "global.h"

using namespace vnotex;

SelectionItemWidget::SelectionItemWidget(const QIcon &p_icon,
                                         const QString &p_text,
                                         QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI(p_text);

    setIcon(p_icon);
}

SelectionItemWidget::SelectionItemWidget(const QString &p_text,
                                         QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI(p_text);
}

void SelectionItemWidget::setupUI(const QString &p_text)
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(CONTENTS_MARGIN, 0, 0, 0);

    m_checkBox = new QCheckBox(this);
    mainLayout->addWidget(m_checkBox);
    connect(m_checkBox, &QCheckBox::stateChanged,
            this, &SelectionItemWidget::checkStateChanged);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setVisible(false);
    mainLayout->addWidget(m_iconLabel);

    m_textLabel = new QLabel(p_text, this);
    mainLayout->addWidget(m_textLabel);

    mainLayout->addStretch();
}

bool SelectionItemWidget::isChecked() const
{
    return m_checkBox->isChecked();
}

const QVariant &SelectionItemWidget::getData() const
{
    return m_data;
}

void SelectionItemWidget::setChecked(bool p_checked)
{
    m_checkBox->setChecked(p_checked);
}

void SelectionItemWidget::setToolTip(const QString &p_tip)
{
    m_textLabel->setToolTip(p_tip);
}

void SelectionItemWidget::setData(const QVariant &p_data)
{
    m_data = p_data;
}

void SelectionItemWidget::setIcon(const QIcon &p_icon)
{
    if (!p_icon.isNull()) {
        int iconHeight = qMax(m_textLabel->minimumSizeHint().height(), 16);
        auto pixmap = p_icon.pixmap(QSize(iconHeight, iconHeight));
        m_iconLabel->setPixmap(pixmap);
    }

    m_iconLabel->setVisible(!p_icon.isNull());
}

void SelectionItemWidget::mouseDoubleClickEvent(QMouseEvent *p_event)
{
    p_event->accept();
    setChecked(!isChecked());
}
