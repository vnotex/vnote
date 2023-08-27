#include "quickaccesspage.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QDebug>
#include <QFileDialog>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QInputDialog>

#include <core/sessionconfig.h>
#include <core/coreconfig.h>
#include <core/configmgr.h>
#include <core/notebookmgr.h>
#include <core/vnotex.h>
#include <utils/widgetutils.h>
#include <widgets/locationinputwithbrowsebutton.h>
#include <widgets/lineeditwithsnippet.h>
#include <widgets/widgetsfactory.h>
#include <widgets/messageboxhelper.h>

#include "../notetemplateselector.h"

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

    auto quickNoteBox = setupQuickNoteGroup();
    mainLayout->addWidget(quickNoteBox);

    mainLayout->addStretch();
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

    loadQuickNoteSchemes();
}

void QuickAccessPage::loadQuickNoteSchemes()
{
    const auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();

    m_quickNoteSchemes = sessionConfig.getQuickNoteSchemes();
    m_quickNoteCurrentIndex = -1;

    m_quickNoteSchemeComboBox->clear();
    for (const auto &scheme : m_quickNoteSchemes) {
        m_quickNoteSchemeComboBox->addItem(scheme.m_name);
    }
    if (m_quickNoteSchemeComboBox->count() > 0) {
        m_quickNoteSchemeComboBox->setCurrentIndex(0);
        // Manually call the handler.
        setCurrentQuickNote(0);
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

    saveQuickNoteSchemes();

    return true;
}

void QuickAccessPage::saveQuickNoteSchemes()
{
    // Save current quick note scheme from inputs.
    saveCurrentQuickNote();

    auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
    sessionConfig.setQuickNoteSchemes(m_quickNoteSchemes);
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
        m_quickAccessTextEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

        const QString label(tr("Quick Access:"));
        layout->addRow(label, m_quickAccessTextEdit);
        addSearchItem(label, m_quickAccessTextEdit->toolTip(), m_quickAccessTextEdit);
        connect(m_quickAccessTextEdit, &QPlainTextEdit::textChanged,
                this, &QuickAccessPage::pageIsChanged);
    }

    return box;
}

QString QuickAccessPage::getDefaultQuickNoteFolderPath()
{
    auto defaultPath = QDir::homePath();
    auto currentNotebook = VNoteX::getInst().getNotebookMgr().getCurrentNotebook();
    if (currentNotebook) {
        defaultPath = currentNotebook->getRootFolderAbsolutePath();
    }
    return defaultPath;
}

QGroupBox *QuickAccessPage::setupQuickNoteGroup()
{
    auto box = new QGroupBox(tr("Quick Note"), this);
    auto mainLayout = WidgetsFactory::createFormLayout(box);

    {
        auto selectorLayout = new QHBoxLayout();

        // Add items in loadInternal().
        m_quickNoteSchemeComboBox = WidgetsFactory::createComboBox(box);
        selectorLayout->addWidget(m_quickNoteSchemeComboBox, 1);
        m_quickNoteSchemeComboBox->setPlaceholderText(tr("No scheme to show"));

        auto newBtn = new QPushButton(tr("New"), box);
        connect(newBtn, &QPushButton::clicked,
                this, &QuickAccessPage::newQuickNoteScheme);
        selectorLayout->addWidget(newBtn);

        auto deleteBtn = new QPushButton(tr("Delete"), box);
        deleteBtn->setEnabled(false);
        connect(deleteBtn, &QPushButton::clicked,
                this, &QuickAccessPage::removeQuickNoteScheme);
        selectorLayout->addWidget(deleteBtn);

        const QString label(tr("Scheme:"));
        mainLayout->addRow(label, selectorLayout);
        addSearchItem(label, m_quickNoteSchemeComboBox);

        connect(m_quickNoteSchemeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &QuickAccessPage::pageIsChanged);
        connect(m_quickNoteSchemeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [deleteBtn](int idx) {
                    deleteBtn->setEnabled(idx > -1);
                });
    }

    m_quickNoteInfoGroupBox = new QGroupBox(box);
    mainLayout->addRow(m_quickNoteInfoGroupBox);
    auto infoLayout = WidgetsFactory::createFormLayout(m_quickNoteInfoGroupBox);

    {
        const QString label(tr("Folder:"));
        m_quickNoteFolderPathInput = new LocationInputWithBrowseButton(m_quickNoteInfoGroupBox);
        m_quickNoteFolderPathInput->setPlaceholderText(tr("Empty to use current explored folder dynamically"));
        infoLayout->addRow(label, m_quickNoteFolderPathInput);
        addSearchItem(label, m_quickNoteFolderPathInput);
        connect(m_quickNoteFolderPathInput, &LocationInputWithBrowseButton::textChanged,
                this, &QuickAccessPage::pageIsChanged);
        connect(m_quickNoteFolderPathInput, &LocationInputWithBrowseButton::clicked,
                this, [this]() {
                    auto folderPath = QFileDialog::getExistingDirectory(this,
                                                                        tr("Select Quick Note Folder"),
                                                                        getDefaultQuickNoteFolderPath());
                    if (!folderPath.isEmpty()) {
                        m_quickNoteFolderPathInput->setText(folderPath);
                    }
                });
    }

    {
        const QString label(tr("Note name:"));
        m_quickNoteNoteNameLineEdit = WidgetsFactory::createLineEditWithSnippet(m_quickNoteInfoGroupBox);
        infoLayout->addRow(label, m_quickNoteNoteNameLineEdit);
        connect(m_quickNoteNoteNameLineEdit, &QLineEdit::textChanged,
                this, &QuickAccessPage::pageIsChanged);
    }

    {
        const QString label(tr("Note template:"));
        m_quickNoteTemplateSelector = new NoteTemplateSelector(m_quickNoteInfoGroupBox);
        infoLayout->addRow(label, m_quickNoteTemplateSelector);
        connect(m_quickNoteTemplateSelector, &NoteTemplateSelector::templateChanged,
                this, &QuickAccessPage::pageIsChanged);
    }

    m_quickNoteInfoGroupBox->setVisible(false);
    connect(m_quickNoteSchemeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                if (isLoading()) {
                    return;
                }
                setCurrentQuickNote(idx);
            });
    return box;
}

void QuickAccessPage::newQuickNoteScheme()
{
    bool isDuplicated = false;
    QString schemeName;
    do {
        schemeName = QInputDialog::getText(this, tr("Quick Note Scheme"),
                isDuplicated ? tr("Scheme name already exists! Try again:") : tr("Scheme name:"));
        if (schemeName.isEmpty()) {
            return;
        }
        isDuplicated = m_quickNoteSchemeComboBox->findText(schemeName) != -1;
    } while (isDuplicated);

    SessionConfig::QuickNoteScheme scheme;
    scheme.m_name = schemeName;
    scheme.m_folderPath = getDefaultQuickNoteFolderPath();
    scheme.m_noteName = tr("quick_note_%da%.md");
    m_quickNoteSchemes.push_back(scheme);

    m_quickNoteSchemeComboBox->addItem(schemeName);
    m_quickNoteSchemeComboBox->setCurrentText(schemeName);

    emit pageIsChanged();
}

void QuickAccessPage::removeQuickNoteScheme()
{
    int idx = m_quickNoteSchemeComboBox->currentIndex();
    Q_ASSERT(idx > -1);

    auto& scheme = m_quickNoteSchemes[idx];
    int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Type::Question,
                                                 tr("Delete quick note scheme (%1)?").arg(scheme.m_name));
    if (ret != QMessageBox::Ok) {
        return;
    }

    m_quickNoteCurrentIndex = -1;
    m_quickNoteSchemes.removeAt(idx);
    m_quickNoteSchemeComboBox->removeItem(idx);
    emit pageIsChanged();
}

void QuickAccessPage::saveCurrentQuickNote()
{
    if (m_quickNoteCurrentIndex < 0) {
        return;
    }
    Q_ASSERT(m_quickNoteCurrentIndex < m_quickNoteSchemes.size());
    auto& scheme = m_quickNoteSchemes[m_quickNoteCurrentIndex];
    scheme.m_folderPath = m_quickNoteFolderPathInput->text();
    // No need to apply the snippet for now.
    scheme.m_noteName = m_quickNoteNoteNameLineEdit->text();
    scheme.m_template = m_quickNoteTemplateSelector->getCurrentTemplate();
}

void QuickAccessPage::loadCurrentQuickNote()
{
    if (m_quickNoteCurrentIndex < 0) {
        m_quickNoteFolderPathInput->setText(QString());
        m_quickNoteNoteNameLineEdit->setText(QString());
        m_quickNoteTemplateSelector->setCurrentTemplate(QString());
        return;
    }

    Q_ASSERT(m_quickNoteCurrentIndex < m_quickNoteSchemes.size());
    const auto& scheme = m_quickNoteSchemes[m_quickNoteCurrentIndex];
    m_quickNoteFolderPathInput->setText(scheme.m_folderPath);
    m_quickNoteNoteNameLineEdit->setText(scheme.m_noteName);
    m_quickNoteTemplateSelector->setCurrentTemplate(scheme.m_template);
}

void QuickAccessPage::setCurrentQuickNote(int idx)
{
    saveCurrentQuickNote();
    m_quickNoteCurrentIndex = idx;
    loadCurrentQuickNote();

    m_quickNoteInfoGroupBox->setVisible(idx > -1);
}
