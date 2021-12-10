#include "terminalviewer.h"

#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QToolButton>

#include <utils/widgetutils.h>

#include "widgetsfactory.h"
#include "titlebar.h"

using namespace vnotex;

TerminalViewer::TerminalViewer(QWidget *p_parent)
    : QFrame(p_parent)
{
    setupUI();
}

void TerminalViewer::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);

    {
        setupTitleBar(QString(), this);
        mainLayout->addWidget(m_titleBar);
    }

    m_consoleEdit = new QPlainTextEdit(this);
    m_consoleEdit->setReadOnly(true);
    mainLayout->addWidget(m_consoleEdit);

    setFocusProxy(m_consoleEdit);
}

void TerminalViewer::setupTitleBar(const QString &p_title, QWidget *p_parent)
{
    m_titleBar = new TitleBar(p_title, true, TitleBar::Action::None, p_parent);
    m_titleBar->setActionButtonsAlwaysShown(true);

    {
        auto clearBtn = m_titleBar->addActionButton(QStringLiteral("clear.svg"), tr("Clear"));
        connect(clearBtn, &QToolButton::triggered,
                this, &TerminalViewer::clear);
    }
}

void TerminalViewer::append(const QString &p_text)
{
    m_consoleEdit->appendPlainText(p_text);
    auto scrollBar = m_consoleEdit->verticalScrollBar();
    if (scrollBar) {
        scrollBar->setValue(scrollBar->maximum());
    }
}

void TerminalViewer::clear()
{
    m_consoleEdit->clear();
}
