#include "wordcountpopup.h"

#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QPointer>

#include <utils/widgetutils.h>

using namespace vnotex;

WordCountPanel::WordCountPanel(QWidget *p_parent)
    : QWidget(p_parent)
{
    auto mainLayout = new QFormLayout(this);

    m_selectionLabel = new QLabel(tr("Selection Area"), this);
    mainLayout->addRow(m_selectionLabel);
    m_selectionLabel->hide();

    const auto alignment = Qt::AlignRight | Qt::AlignVCenter;
    m_wordLabel = new QLabel("0", this);
    m_wordLabel->setAlignment(alignment);
    mainLayout->addRow(tr("Words"), m_wordLabel);

    m_charWithoutSpaceLabel = new QLabel("0", this);
    m_charWithoutSpaceLabel->setAlignment(alignment);
    mainLayout->addRow(tr("Characters (no spaces)"), m_charWithoutSpaceLabel);

    m_charWithSpaceLabel = new QLabel("0", this);
    m_charWithSpaceLabel->setAlignment(alignment);
    mainLayout->addRow(tr("Characters (with spaces)"), m_charWithSpaceLabel);
}

void WordCountPanel::updateCount(bool p_isSelection,
                                 int p_words,
                                 int p_charsWithoutSpace,
                                 int p_charsWithSpace)
{
    m_selectionLabel->setVisible(p_isSelection);
    m_wordLabel->setText(QString::number(p_words));
    m_charWithoutSpaceLabel->setText(QString::number(p_charsWithoutSpace));
    m_charWithSpaceLabel->setText(QString::number(p_charsWithSpace));
}

WordCountPopup::WordCountPopup(QToolButton *p_btn, const ViewWindow *p_viewWindow, QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent),
      m_viewWindow(p_viewWindow)
{
    setupUI();

    connect(this, &QMenu::aboutToShow,
            this, [this]() {
                QPointer<WordCountPopup> popup(this);
                m_viewWindow->fetchWordCountInfo([popup](const ViewWindow::WordCountInfo &info) {
                    if (popup) {
                        popup->updateCount(info);
                    }
                });
            });
}

void WordCountPopup::updateCount(const ViewWindow::WordCountInfo &p_info)
{
    m_panel->updateCount(p_info.m_isSelection,
                         p_info.m_wordCount,
                         p_info.m_charWithoutSpaceCount,
                         p_info.m_charWithSpaceCount);
}

void WordCountPopup::setupUI()
{
    QWidget *mainWidget = new QWidget(this);
    setCentralWidget(mainWidget);

    auto mainLayout = new QVBoxLayout(mainWidget);

    m_panel = new WordCountPanel(mainWidget);
    mainLayout->addWidget(m_panel);
}
