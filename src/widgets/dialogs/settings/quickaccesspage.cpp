#include "quickaccesspage.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QDebug>

#include <widgets/widgetsfactory.h>
#include <core/sessionconfig.h>
#include <core/coreconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>
#include <widgets/locationinputwithbrowsebutton.h>

using namespace vnotex;

QuickAccessPage::QuickAccessPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void QuickAccessPage::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    auto flashPageBox = setupFlashPageGroup();
    mainLayout->addWidget(flashPageBox);

    auto quickAccessBox = setupQuickAccessGroup();
    mainLayout->addWidget(quickAccessBox);
}

void QuickAccessPage::loadInternal()
{
    const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();

    m_flashPageInput->setText(sessionConfig.getFlashPage());

    {
        const auto &quickAccess = sessionConfig.getQuickAccessFiles();
        if (!quickAccess.isEmpty()) {
            m_quickAccessTextEdit->setPlainText(quickAccess.join(QChar('\n')));
        }
    }
}

void QuickAccessPage::saveInternal()
{
    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();

    sessionConfig.setFlashPage(m_flashPageInput->text());

    {
        auto text = m_quickAccessTextEdit->toPlainText();
        if (!text.isEmpty()) {
            sessionConfig.setQuickAccessFiles(text.split(QChar('\n')));
        }
    }
}

QString QuickAccessPage::title() const
{
    return tr("Quick Access");
}

QGroupBox *QuickAccessPage::setupFlashPageGroup()
{
    auto box = new QGroupBox(tr("Flash Page"), this);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        m_flashPageInput = new LocationInputWithBrowseButton(box);
        m_flashPageInput->setToolTip(tr("Flash Page location (user could copy the path of one note and paste it here)"));

        const QString label(tr("Flash Page:"));
        layout->addRow(label, m_flashPageInput);
        addSearchItem(label, m_flashPageInput->toolTip(), m_flashPageInput);
        connect(m_flashPageInput, &LocationInputWithBrowseButton::textChanged,
                this, &QuickAccessPage::pageIsChanged);
    }

    return box;
}

QGroupBox *QuickAccessPage::setupQuickAccessGroup()
{
    auto box = new QGroupBox(tr("Quick Access"), this);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        m_quickAccessTextEdit = WidgetsFactory::createPlainTextEdit(box);
        m_quickAccessTextEdit->setToolTip(tr("Edit the files pinned to Quick Access (one file per line)"));

        const QString label(tr("Quick Access:"));
        layout->addRow(label, m_quickAccessTextEdit);
        addSearchItem(label, m_quickAccessTextEdit->toolTip(), m_quickAccessTextEdit);
        connect(m_quickAccessTextEdit, &QPlainTextEdit::textChanged,
                this, &QuickAccessPage::pageIsChanged);
    }

    return box;
}
