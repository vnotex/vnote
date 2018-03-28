#include "vuniversalentry.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QDebug>
#include <QHideEvent>
#include <QShowEvent>

#include "vlineedit.h"
#include "utils/vutils.h"

#define MINIMUM_WIDTH 200

VUniversalEntry::VUniversalEntry(QWidget *p_parent)
    : QWidget(p_parent),
      m_availableRect(0, 0, MINIMUM_WIDTH, MINIMUM_WIDTH)
{
    m_minimumWidth = MINIMUM_WIDTH * VUtils::calculateScaleFactor() + 0.5;
    setupUI();
}

void VUniversalEntry::setupUI()
{
    m_cmdEdit = new VLineEdit(this);
    m_cmdEdit->setPlaceholderText(tr("Welcome to Universal Entry"));

    m_layout = new QVBoxLayout();
    m_layout->addWidget(m_cmdEdit);

    m_layout->setContentsMargins(0, 0, 0, 0);

	setLayout(m_layout);

    setMinimumWidth(m_minimumWidth);
}

void VUniversalEntry::hideEvent(QHideEvent *p_event)
{
    QWidget::hideEvent(p_event);
    emit exited();
}

void VUniversalEntry::showEvent(QShowEvent *p_event)
{
    QWidget::showEvent(p_event);

    m_cmdEdit->setFocus();
    m_cmdEdit->selectAll();
}

void VUniversalEntry::setAvailableRect(const QRect &p_rect)
{
    m_availableRect = p_rect;

    if (m_availableRect.width() < m_minimumWidth) {
        m_availableRect.setWidth(m_minimumWidth);
    }
}
