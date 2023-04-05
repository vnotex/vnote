#include "quickaccesspage.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QDebug>
#include <QFileDialog>
#include <QCheckBox>

#include <widgets/widgetsfactory.h>
#include "core/vnotex.h"
#include <core/notebookmgr.h>
#include <core/sessionconfig.h>
#include <core/coreconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>
#include <widgets/locationinputwithbrowsebutton.h>

#include "core/buffer/filetypehelper.h"

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

    auto quickCreateNoteBox = setupQuickNotePageGroup();
    mainLayout->addWidget(quickCreateNoteBox);

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

    {
        m_quickNoteStorePath->setText(sessionConfig.getQuickNoteStorePath());

        for(int typ : sessionConfig.getQuickNoteType()) {
            if (FileType::Markdown == typ) {
                m_quickMarkdownCheckBox->setChecked(true);
            }
            if (FileType::Text == typ) {
                m_quickTextCheckBox->setChecked(true);
            }
            if (FileType::MindMap == typ) {
                m_quickMindmapCheckBox->setChecked(true);
            }
        }
    }
}

bool QuickAccessPage::saveInternal()
{
    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();

    sessionConfig.setFlashPage(m_flashPageInput->text());

    {
        auto text = m_quickAccessTextEdit->toPlainText();
        if (!text.isEmpty()) {
            sessionConfig.setQuickAccessFiles(text.split(QChar('\n')));
        }
    }

    {
        auto storePath = m_quickNoteStorePath->text();
        if (!storePath.isEmpty()) {
            sessionConfig.setQuickNoteStorePath(static_cast<QString>(storePath));
        }

        QVector<int> createModeList = QVector<int>();
        if (m_quickMarkdownCheckBox->isChecked()) {
            createModeList.append(FileType::Markdown);
        }
        if (m_quickTextCheckBox->isChecked()) {
            createModeList.append(FileType::Text);
        }
        if (m_quickMindmapCheckBox->isChecked()) {
            createModeList.append(FileType::MindMap);
        }
        sessionConfig.setQuickNoteType(createModeList);
    }

    return true;
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
        connect(m_flashPageInput, &LocationInputWithBrowseButton::clicked,
                this, [this]() {
                    auto filePath = QFileDialog::getOpenFileName(this,
                                                                 tr("Select Flash Page File"),
                                                                 QDir::homePath());
                    if (!filePath.isEmpty()) {
                        m_flashPageInput->setText(filePath);
                    }
                });
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

QGroupBox *QuickAccessPage::setupQuickNotePageGroup()
{
    auto box = new QGroupBox(tr("Quick Note"), this);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        m_quickNoteStorePath = new LocationInputWithBrowseButton(box);
        m_quickNoteStorePath->setToolTip(tr("Quick Note storage path (default path is current notebook root)"));
        // TODO default ?
        if (m_quickNoteStorePath->text().isEmpty()) {
            m_quickNoteStorePath->setText(VNoteX::getInst().getNotebookMgr().getCurrentNotebook()->getRootFolderAbsolutePath());
        }

        const QString storagePathLabel(tr("Storage path:"));
        layout->addRow(storagePathLabel, m_quickNoteStorePath);
        addSearchItem(storagePathLabel, m_quickNoteStorePath->toolTip(), m_quickNoteStorePath);
        connect(m_quickNoteStorePath, &LocationInputWithBrowseButton::textChanged,
                this, &QuickAccessPage::pageIsChanged);
        connect(m_quickNoteStorePath, &LocationInputWithBrowseButton::clicked,
                this, [this]() {
                    auto notebookRootPath = VNoteX::getInst().getNotebookMgr().getCurrentNotebook()->getRootFolderAbsolutePath();
                    auto filePath = QFileDialog::getExistingDirectory(this,
                                                                      tr("Select Quick Note storage path"),
                                                                      notebookRootPath);
                    if (!filePath.isEmpty()) {
                        m_quickNoteStorePath->setText(filePath);
                    }
                });

        const QString mdLabel(tr("Markdown"));
        m_quickMarkdownCheckBox = WidgetsFactory::createCheckBox(mdLabel, box);
        m_quickMarkdownCheckBox->setToolTip(tr("Suppet Quick Note for Markdown"));
        layout->addRow(m_quickMarkdownCheckBox);
        addSearchItem(mdLabel, m_quickMarkdownCheckBox->toolTip(), m_quickMarkdownCheckBox);
        connect(m_quickMarkdownCheckBox, &QCheckBox::stateChanged,
                this, &QuickAccessPage::pageIsChanged);

        const QString txtlabel(tr("Text"));
        m_quickTextCheckBox = WidgetsFactory::createCheckBox(txtlabel, box);
        m_quickTextCheckBox->setToolTip(tr("Suppet Quick note for Text"));
        layout->addRow(m_quickTextCheckBox);
        addSearchItem(txtlabel, m_quickTextCheckBox->toolTip(), m_quickTextCheckBox);
        connect(m_quickTextCheckBox, &QCheckBox::stateChanged,
                this, &QuickAccessPage::pageIsChanged);

        const QString mindmapLabel(tr("Mind Map"));
        m_quickMindmapCheckBox = WidgetsFactory::createCheckBox(mindmapLabel, box);
        m_quickMindmapCheckBox->setToolTip(tr("Suppet Quick Note for Mind Map"));
        layout->addRow(m_quickMindmapCheckBox);
        addSearchItem(mindmapLabel, m_quickMindmapCheckBox->toolTip(), m_quickMindmapCheckBox);
        connect(m_quickMindmapCheckBox, &QCheckBox::stateChanged,
                this, &QuickAccessPage::pageIsChanged);
    }

    return box;
}
