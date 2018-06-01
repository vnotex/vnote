#include "vsettingsdialog.h"
#include <QtWidgets>
#include <QRegExp>
#include <QToolTip>

#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vconstants.h"
#include "vlineedit.h"

extern VConfigManager *g_config;

VSettingsDialog::VSettingsDialog(QWidget *p_parent)
    : QDialog(p_parent)
{
    m_tabList = new QListWidget(this);
    m_tabList->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    m_tabs = new QStackedLayout();

    // Reset VNote.
    m_resetVNoteBtn = new QPushButton(tr("Reset VNote"), this);
    m_resetVNoteBtn->setProperty("DangerBtn", true);
    m_resetVNoteBtn->setToolTip(tr("Reset all the configurations of VNote"));
    connect(m_resetVNoteBtn, &QPushButton::clicked,
            this, &VSettingsDialog::resetVNote);

    // Reset Layout.
    m_resetLayoutBtn = new QPushButton(tr("Reset Layout"), this);
    m_resetLayoutBtn->setToolTip(tr("Reset layout of VNote"));
    connect(m_resetLayoutBtn, &QPushButton::clicked,
            this, &VSettingsDialog::resetLayout);

    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &VSettingsDialog::saveConfiguration);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_btnBox->button(QDialogButtonBox::Ok)->setProperty("SpecialBtn", true);

    m_btnBox->addButton(m_resetVNoteBtn, QDialogButtonBox::ResetRole);
    m_btnBox->addButton(m_resetLayoutBtn, QDialogButtonBox::ResetRole);

    QHBoxLayout *tabLayout = new QHBoxLayout();
    tabLayout->addWidget(m_tabList);
    tabLayout->addLayout(m_tabs);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);
    tabLayout->setStretch(0, 0);
    tabLayout->setStretch(1, 5);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(tabLayout);
    mainLayout->addWidget(m_btnBox);
    setLayout(mainLayout);

    setWindowTitle(tr("Settings"));

    // Add tabs.
    addTab(new VGeneralTab(), tr("General"));
    addTab(new VLookTab(), tr("Appearance"));
    addTab(new VReadEditTab(), tr("Read/Edit"));
    addTab(new VNoteManagementTab(), tr("Note Management"));
    addTab(new VMarkdownTab(), tr("Markdown"));

    m_tabList->setMaximumWidth(m_tabList->sizeHintForColumn(0) + 5);

    connect(m_tabList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *p_cur, QListWidgetItem *p_pre) {
                Q_UNUSED(p_pre);
                Q_ASSERT(p_cur);
                int idx = p_cur->data(Qt::UserRole).toInt();
                Q_ASSERT(idx >= 0);
                m_tabs->setCurrentWidget(m_tabs->widget(idx));
            });

    m_tabList->setCurrentRow(0);

    loadConfiguration();
}

void VSettingsDialog::resetVNote()
{
    int ret = VUtils::showMessage(QMessageBox::Warning,
                                  tr("Warning"),
                                  tr("Are you sure to reset VNote?"),
                                  tr("All configurations (except notebooks information) "
                                     "will be reset to default values. "
                                     "It is UNRECOVERABLE!"),
                                  QMessageBox::Ok | QMessageBox::Cancel,
                                  QMessageBox::Cancel,
                                  this,
                                  MessageBoxType::Danger);

    if (ret == QMessageBox::Cancel) {
        return;
    }

    g_config->resetConfigurations();

    VUtils::showMessage(QMessageBox::Information,
                        tr("Information"),
                        tr("Please restart VNote to make it work."),
                        tr("Any change to VNote before restart will be lost!"),
                        QMessageBox::Ok,
                        QMessageBox::Ok,
                        this);

    reject();
}

void VSettingsDialog::resetLayout()
{
    int ret = VUtils::showMessage(QMessageBox::Warning,
                                  tr("Warning"),
                                  tr("Are you sure to reset the layout of VNote?"),
                                  tr("The view and layout mode will be reset. "
                                     "It is UNRECOVERABLE!"),
                                  QMessageBox::Ok | QMessageBox::Cancel,
                                  QMessageBox::Cancel,
                                  this,
                                  MessageBoxType::Danger);

    if (ret == QMessageBox::Cancel) {
        return;
    }

    g_config->resetLayoutConfigurations();

    VUtils::showMessage(QMessageBox::Information,
                        tr("Information"),
                        tr("Please restart VNote to make it work."),
                        tr("Any change to VNote before restart will be lost!"),
                        QMessageBox::Ok,
                        QMessageBox::Ok,
                        this);

    reject();
}

void VSettingsDialog::addTab(QWidget *p_widget, const QString &p_label)
{
    int idx = m_tabs->addWidget(p_widget);
    QListWidgetItem *item = new QListWidgetItem(p_label, m_tabList);
    item->setData(Qt::UserRole, idx);
}

void VSettingsDialog::loadConfiguration()
{
    int idx = 0;
    // General Tab.
    {
        VGeneralTab *generalTab = dynamic_cast<VGeneralTab *>(m_tabs->widget(idx++));
        Q_ASSERT(generalTab);
        if (!generalTab->loadConfiguration()) {
            goto err;
        }
    }

    // Appearance Tab.
    {
        VLookTab *lookTab = dynamic_cast<VLookTab *>(m_tabs->widget(idx++));
        Q_ASSERT(lookTab);
        if (!lookTab->loadConfiguration()) {
            goto err;
        }
    }

    // Read/Edit Tab.
    {
        VReadEditTab *readEditTab = dynamic_cast<VReadEditTab *>(m_tabs->widget(idx++));
        Q_ASSERT(readEditTab);
        if (!readEditTab->loadConfiguration()) {
            goto err;
        }
    }

    // Note Management Tab.
    {
        VNoteManagementTab *noteManagementTab = dynamic_cast<VNoteManagementTab *>(m_tabs->widget(idx++));
        Q_ASSERT(noteManagementTab);
        if (!noteManagementTab->loadConfiguration()) {
            goto err;
        }
    }

    // Markdown Tab.
    {
        VMarkdownTab *markdownTab = dynamic_cast<VMarkdownTab *>(m_tabs->widget(idx++));
        Q_ASSERT(markdownTab);
        if (!markdownTab->loadConfiguration()) {
            goto err;
        }
    }

    return;
err:
    VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                        tr("Fail to load configuration."), "",
                        QMessageBox::Ok, QMessageBox::Ok, NULL);
    QMetaObject::invokeMethod(this, "reject", Qt::QueuedConnection);
}

void VSettingsDialog::saveConfiguration()
{
    int idx = 0;
    // General Tab.
    {
        VGeneralTab *generalTab = dynamic_cast<VGeneralTab *>(m_tabs->widget(idx++));
        Q_ASSERT(generalTab);
        if (!generalTab->saveConfiguration()) {
            goto err;
        }
    }

    // Appearance Tab.
    {
        VLookTab *lookTab = dynamic_cast<VLookTab *>(m_tabs->widget(idx++));
        Q_ASSERT(lookTab);
        if (!lookTab->saveConfiguration()) {
            goto err;
        }
    }

    // Read/Edit Tab.
    {
        VReadEditTab *readEditTab = dynamic_cast<VReadEditTab *>(m_tabs->widget(idx++));
        Q_ASSERT(readEditTab);
        if (!readEditTab->saveConfiguration()) {
            goto err;
        }
    }

    // Note Management Tab.
    {
        VNoteManagementTab *noteManagementTab = dynamic_cast<VNoteManagementTab *>(m_tabs->widget(idx++));
        Q_ASSERT(noteManagementTab);
        if (!noteManagementTab->saveConfiguration()) {
            goto err;
        }
    }

    // Markdown Tab.
    {
        VMarkdownTab *markdownTab = dynamic_cast<VMarkdownTab *>(m_tabs->widget(idx++));
        Q_ASSERT(markdownTab);
        if (!markdownTab->saveConfiguration()) {
            goto err;
        }
    }

    accept();
    return;
err:
    VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                        tr("Fail to save configuration. Please try it again."), "",
                        QMessageBox::Ok, QMessageBox::Ok, NULL);
}

const QVector<QString> VGeneralTab::c_availableLangs = { "System", "English", "Chinese" };

VGeneralTab::VGeneralTab(QWidget *p_parent)
    : QWidget(p_parent)
{
    // Language combo.
    m_langCombo = VUtils::getComboBox(this);
    m_langCombo->setToolTip(tr("Choose the language of VNote interface"));
    m_langCombo->addItem(tr("System"), "System");
    auto langs = VUtils::getAvailableLanguages();
    for (auto const &lang : langs) {
        m_langCombo->addItem(lang.second, lang.first);
    }

    // System tray checkbox.
    m_systemTray = new QCheckBox(tr("System tray"), this);
    m_systemTray->setToolTip(tr("Minimized to the system tray after closing VNote"
                                " (not supported in macOS)"));
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Do not support minimized to tray on macOS.
    m_systemTray->setEnabled(false);
#endif

    // Startup pages.
    QLayout *startupLayout = setupStartupPagesLayout();

    QFormLayout *optionLayout = new QFormLayout();
    optionLayout->addRow(tr("Language:"), m_langCombo);
    optionLayout->addRow(m_systemTray);
    optionLayout->addRow(tr("Startup pages:"), startupLayout);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(optionLayout);

    setLayout(mainLayout);
}

QLayout *VGeneralTab::setupStartupPagesLayout()
{
    m_startupPageTypeCombo = VUtils::getComboBox(this);
    m_startupPageTypeCombo->setToolTip(tr("Restore tabs or open specific notes on startup"));
    m_startupPageTypeCombo->addItem(tr("None"), (int)StartupPageType::None);
    m_startupPageTypeCombo->addItem(tr("Continue where you left off"), (int)StartupPageType::ContinueLeftOff);
    m_startupPageTypeCombo->addItem(tr("Open specific pages"), (int)StartupPageType::SpecificPages);
    connect(m_startupPageTypeCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
            this, [this](int p_index) {
                int type = m_startupPageTypeCombo->itemData(p_index).toInt();
                bool pagesEditVisible = type == (int)StartupPageType::SpecificPages;
                m_startupPagesEdit->setVisible(pagesEditVisible);
                m_startupPagesAddBtn->setVisible(pagesEditVisible);
            });

    m_startupPagesEdit = new QPlainTextEdit(this);
    m_startupPagesEdit->setProperty("LineEdit", true);
    m_startupPagesEdit->setToolTip(tr("Absolute path of the notes to open on startup (one note per line)"));

    m_startupPagesAddBtn = new QPushButton(tr("Browse"), this);
    m_startupPagesAddBtn->setToolTip(tr("Select files to add as startup pages"));
    connect(m_startupPagesAddBtn, &QPushButton::clicked,
            this, [this]() {
                static QString lastPath = QDir::homePath();
                QStringList files = QFileDialog::getOpenFileNames(this,
                                                                  tr("Select Files As Startup Pages"),
                                                                  lastPath);
                if (files.isEmpty()) {
                    return;
                }

                // Update lastPath
                lastPath = QFileInfo(files[0]).path();

                m_startupPagesEdit->appendPlainText(files.join("\n"));
            });

    QHBoxLayout *startupPagesBtnLayout = new QHBoxLayout();
    startupPagesBtnLayout->addStretch();
    startupPagesBtnLayout->addWidget(m_startupPagesAddBtn);

    QVBoxLayout *startupPagesLayout = new QVBoxLayout();
    startupPagesLayout->addWidget(m_startupPagesEdit);
    startupPagesLayout->addLayout(startupPagesBtnLayout);

    QVBoxLayout *startupLayout = new QVBoxLayout();
    startupLayout->addWidget(m_startupPageTypeCombo);
    startupLayout->addLayout(startupPagesLayout);

    m_startupPagesEdit->hide();
    m_startupPagesAddBtn->hide();

    return startupLayout;
}

bool VGeneralTab::loadConfiguration()
{
    if (!loadLanguage()) {
        return false;
    }

    if (!loadSystemTray()) {
        return false;
    }

    if (!loadStartupPageType()) {
        return false;
    }

    return true;
}

bool VGeneralTab::saveConfiguration()
{
    if (!saveLanguage()) {
        return false;
    }

    if (!saveSystemTray()) {
        return false;
    }

    if (!saveStartupPageType()) {
        return false;
    }

    return true;
}

bool VGeneralTab::loadLanguage()
{
    QString lang = g_config->getLanguage();
    if (lang.isNull()) {
        return false;
    } else if (lang == "System") {
        m_langCombo->setCurrentIndex(0);
        return true;
    }
    bool found = false;
    // lang is the value, not name.
    for (int i = 0; i < m_langCombo->count(); ++i) {
        if (m_langCombo->itemData(i).toString() == lang) {
            found = true;
            m_langCombo->setCurrentIndex(i);
            break;
        }
    }
    if (!found) {
        qWarning() << "invalid language configuration (using default value)";
        m_langCombo->setCurrentIndex(0);
    }
    return true;
}

bool VGeneralTab::saveLanguage()
{
    QString curLang = m_langCombo->currentData().toString();
    g_config->setLanguage(curLang);
    return true;
}

bool VGeneralTab::loadSystemTray()
{
    m_systemTray->setChecked(g_config->getMinimizeToStystemTray() != 0);
    return true;
}

bool VGeneralTab::saveSystemTray()
{
    if (m_systemTray->isEnabled()) {
        g_config->setMinimizeToSystemTray(m_systemTray->isChecked() ? 1 : 0);
    }

    return true;
}

bool VGeneralTab::loadStartupPageType()
{
    StartupPageType type = g_config->getStartupPageType();
    bool found = false;
    for (int i = 0; i < m_startupPageTypeCombo->count(); ++i) {
        if (m_startupPageTypeCombo->itemData(i).toInt() == (int)type) {
            found = true;
            m_startupPageTypeCombo->setCurrentIndex(i);
        }
    }

    Q_ASSERT(found);

    const QStringList &pages = g_config->getStartupPages();
    m_startupPagesEdit->setPlainText(pages.join("\n"));

    bool pagesEditVisible = type == StartupPageType::SpecificPages;
    m_startupPagesEdit->setVisible(pagesEditVisible);
    m_startupPagesAddBtn->setVisible(pagesEditVisible);

    return true;
}

bool VGeneralTab::saveStartupPageType()
{
    StartupPageType type = (StartupPageType)m_startupPageTypeCombo->currentData().toInt();
    g_config->setStartupPageType(type);

    if (type == StartupPageType::SpecificPages) {
        QStringList pages = m_startupPagesEdit->toPlainText().split("\n");
        g_config->setStartupPages(pages);
    }

    return true;
}

VLookTab::VLookTab(QWidget *p_parent)
    : QWidget(p_parent)
{
    m_tbIconSizeSpin = new QSpinBox(this);
    m_tbIconSizeSpin->setToolTip(tr("Icon size in pixel of tool bar (restart VNote to make it work)"));
    m_tbIconSizeSpin->setMaximum(100);
    m_tbIconSizeSpin->setMinimum(5);

    QFormLayout *layout = new QFormLayout();
    layout->addRow(tr("Tool bar icon size:"), m_tbIconSizeSpin);

    setLayout(layout);
}

bool VLookTab::loadConfiguration()
{
    if (!loadToolBarIconSize()) {
        return false;
    }

    return true;
}

bool VLookTab::saveConfiguration()
{
    if (!saveToolBarIconSize()) {
        return false;
    }

    return true;
}

bool VLookTab::loadToolBarIconSize()
{
    int sz = g_config->getToolBarIconSize();
    m_tbIconSizeSpin->setValue(sz);
    return true;
}

bool VLookTab::saveToolBarIconSize()
{
    g_config->setToolBarIconSize(m_tbIconSizeSpin->value());
    return true;
}

VReadEditTab::VReadEditTab(QWidget *p_parent)
    : QWidget(p_parent)
{
    m_readBox = new QGroupBox(tr("Read Mode (For Markdown Only)"));
    m_editBox = new QGroupBox(tr("Edit Mode"));

    // Web Zoom Factor.
    m_customWebZoom = new QCheckBox(tr("Custom Web zoom factor"));
    m_customWebZoom->setToolTip(tr("Set the zoom factor of the Web page when reading"));
    m_webZoomFactorSpin = new QDoubleSpinBox();
    m_webZoomFactorSpin->setMaximum(c_webZoomFactorMax);
    m_webZoomFactorSpin->setMinimum(c_webZoomFactorMin);
    m_webZoomFactorSpin->setSingleStep(0.25);
    connect(m_customWebZoom, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                this->m_webZoomFactorSpin->setEnabled(p_state == Qt::Checked);
            });
    QHBoxLayout *zoomFactorLayout = new QHBoxLayout();
    zoomFactorLayout->addWidget(m_customWebZoom);
    zoomFactorLayout->addWidget(m_webZoomFactorSpin);

    // Web flash anchor.
    m_flashAnchor = new QCheckBox(tr("Flash current heading"));
    m_flashAnchor->setToolTip(tr("Flash current heading on change"));

    // Swap file.
    m_swapFile = new QCheckBox(tr("Swap file"));
    m_swapFile->setToolTip(tr("Automatically save changes to a swap file for backup"));
    connect(m_swapFile, &QCheckBox::stateChanged,
            this, &VReadEditTab::showTipsAboutAutoSave);

    // Auto save.
    m_autoSave = new QCheckBox(tr("Auto save"));
    m_autoSave->setToolTip(tr("Automatically save the note when editing"));
    connect(m_autoSave, &QCheckBox::stateChanged,
            this, &VReadEditTab::showTipsAboutAutoSave);

    // Editor zoom delta.
    m_editorZoomDeltaSpin = new QSpinBox();
    m_editorZoomDeltaSpin->setToolTip(tr("Set the zoom delta of the editor font"));
    m_editorZoomDeltaSpin->setMaximum(c_editorZoomDeltaMax);
    m_editorZoomDeltaSpin->setMinimum(c_editorZoomDeltaMin);
    m_editorZoomDeltaSpin->setSingleStep(1);

    QVBoxLayout *readLayout = new QVBoxLayout();
    readLayout->addLayout(zoomFactorLayout);
    readLayout->addWidget(m_flashAnchor);
    m_readBox->setLayout(readLayout);

    QFormLayout *editLayout = new QFormLayout();
    editLayout->addRow(m_swapFile);
    editLayout->addRow(m_autoSave);
    editLayout->addRow(tr("Editor zoom delta:"), m_editorZoomDeltaSpin);
    m_editBox->setLayout(editLayout);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(m_readBox);
    mainLayout->addWidget(m_editBox);
    setLayout(mainLayout);
}

void VReadEditTab::showTipsAboutAutoSave()
{
    if (m_autoSave->isChecked() && m_swapFile->isChecked()) {
        // Show a tooltip.
        QPoint pos = m_editBox->mapToGlobal(QPoint(0, 0));
        QToolTip::showText(pos,
                           tr("It's recommended to enable \"Swap file\" "
                              "or \"Auto save\", not both"),
                            m_editBox);
    }
}

bool VReadEditTab::loadConfiguration()
{
    if (!loadWebZoomFactor()) {
        return false;
    }

    if (!loadFlashAnchor()) {
        return false;
    }

    if (!loadSwapFile()) {
        return false;
    }

    if (!loadAutoSave()) {
        return false;
    }

    if (!loadEditorZoomDelta()) {
        return false;
    }

    return true;
}

bool VReadEditTab::saveConfiguration()
{
    if (!saveWebZoomFactor()) {
        return false;
    }

    if (!saveFlashAnchor()) {
        return false;
    }

    if (!saveSwapFile()) {
        return false;
    }

    if (!saveAutoSave()) {
        return false;
    }

    if (!saveEditorZoomDelta()) {
        return false;
    }

    return true;
}

bool VReadEditTab::loadWebZoomFactor()
{
    qreal factor = g_config->getWebZoomFactor();
    bool customFactor = g_config->isCustomWebZoomFactor();
    if (customFactor) {
        if (factor < c_webZoomFactorMin || factor > c_webZoomFactorMax) {
            factor = 1;
        }
        m_customWebZoom->setChecked(true);
        m_webZoomFactorSpin->setValue(factor);
    } else {
        m_customWebZoom->setChecked(false);
        m_webZoomFactorSpin->setValue(factor);
        m_webZoomFactorSpin->setEnabled(false);
    }

    return true;
}

bool VReadEditTab::saveWebZoomFactor()
{
    if (m_customWebZoom->isChecked()) {
        g_config->setWebZoomFactor(m_webZoomFactorSpin->value());
    } else {
        g_config->setWebZoomFactor(-1);
    }

    return true;
}

bool VReadEditTab::loadEditorZoomDelta()
{
    m_editorZoomDeltaSpin->setValue(g_config->getEditorZoomDelta());
    return true;
}

bool VReadEditTab::saveEditorZoomDelta()
{
    g_config->setEditorZoomDelta(m_editorZoomDeltaSpin->value());
    return true;
}

bool VReadEditTab::loadFlashAnchor()
{
    m_flashAnchor->setChecked(g_config->getEnableFlashAnchor());
    return true;
}

bool VReadEditTab::saveFlashAnchor()
{
    g_config->setEnableFlashAnchor(m_flashAnchor->isChecked());
    return true;
}

bool VReadEditTab::loadSwapFile()
{
    m_swapFile->setChecked(g_config->getEnableBackupFile());
    return true;
}

bool VReadEditTab::saveSwapFile()
{
    g_config->setEnableBackupFile(m_swapFile->isChecked());
    return true;
}

bool VReadEditTab::loadAutoSave()
{
    m_autoSave->setChecked(g_config->getEnableAutoSave());
    return true;
}

bool VReadEditTab::saveAutoSave()
{
    g_config->setEnableAutoSave(m_autoSave->isChecked());
    return true;
}

VNoteManagementTab::VNoteManagementTab(QWidget *p_parent)
    : QWidget(p_parent)
{
    m_noteBox = new QGroupBox(tr("Notes"));
    m_externalBox = new QGroupBox(tr("External Files"));

    // Note.
    // Image folder.
    m_customImageFolder = new QCheckBox(tr("Custom image folder"), this);
    m_customImageFolder->setToolTip(tr("Set the global name of the image folder to hold images "
                                       "of notes (restart VNote to make it work)"));
    connect(m_customImageFolder, &QCheckBox::stateChanged,
            this, &VNoteManagementTab::customImageFolderChanged);

    m_imageFolderEdit = new VLineEdit(this);
    m_imageFolderEdit->setPlaceholderText(tr("Name of the image folder"));
    m_imageFolderEdit->setToolTip(m_customImageFolder->toolTip());
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp), this);
    m_imageFolderEdit->setValidator(validator);

    QHBoxLayout *imageFolderLayout = new QHBoxLayout();
    imageFolderLayout->addWidget(m_customImageFolder);
    imageFolderLayout->addWidget(m_imageFolderEdit);

    // Attachment folder.
    m_customAttachmentFolder = new QCheckBox(tr("Custom attachment folder"), this);
    m_customAttachmentFolder->setToolTip(tr("Set the global name of the attachment folder to hold attachments "
                                            "of notes (restart VNote to make it work)"));
    connect(m_customAttachmentFolder, &QCheckBox::stateChanged,
            this, &VNoteManagementTab::customAttachmentFolderChanged);

    m_attachmentFolderEdit = new VLineEdit(this);
    m_attachmentFolderEdit->setPlaceholderText(tr("Name of the attachment folder"));
    m_attachmentFolderEdit->setToolTip(m_customAttachmentFolder->toolTip());
    validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp), this);
    m_attachmentFolderEdit->setValidator(validator);

    QHBoxLayout *attachmentFolderLayout = new QHBoxLayout();
    attachmentFolderLayout->addWidget(m_customAttachmentFolder);
    attachmentFolderLayout->addWidget(m_attachmentFolderEdit);

    // Single click open.
    m_singleClickOpen = new QCheckBox(tr("Single click to open a note in current tab"), this);
    m_singleClickOpen->setToolTip(tr("Single click a note in the notes list to open it in current tab, "
                                     "double click to open it in a new tab"));

    QFormLayout *noteLayout = new QFormLayout();
    noteLayout->addRow(imageFolderLayout);
    noteLayout->addRow(attachmentFolderLayout);
    noteLayout->addRow(m_singleClickOpen);
    m_noteBox->setLayout(noteLayout);

    // External File.
    // Image folder.
    m_customImageFolderExt = new QCheckBox(tr("Custom image folder"), this);
    m_customImageFolderExt->setToolTip(tr("Set the path of the global image folder to hold images "
                                          "of external files (restart VNote to make it work).\nYou "
                                          "could use both absolute or relative path here. If "
                                          "absolute path is used, VNote will not manage\nthose images, "
                                          "so you need to clean up unused images manually."));
    connect(m_customImageFolderExt, &QCheckBox::stateChanged,
            this, &VNoteManagementTab::customImageFolderExtChanged);

    m_imageFolderEditExt = new VLineEdit(this);
    m_imageFolderEditExt->setToolTip(m_customImageFolderExt->toolTip());
    m_imageFolderEditExt->setPlaceholderText(tr("Name of the image folder"));

    QHBoxLayout *imageFolderExtLayout = new QHBoxLayout();
    imageFolderExtLayout->addWidget(m_customImageFolderExt);
    imageFolderExtLayout->addWidget(m_imageFolderEditExt);

    QFormLayout *externalLayout = new QFormLayout();
    externalLayout->addRow(imageFolderExtLayout);
    m_externalBox->setLayout(externalLayout);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(m_noteBox);
    mainLayout->addWidget(m_externalBox);

    setLayout(mainLayout);
}

bool VNoteManagementTab::loadConfiguration()
{
    if (!loadImageFolder()) {
        return false;
    }

    if (!loadAttachmentFolder()) {
        return false;
    }

    if (!loadImageFolderExt()) {
        return false;
    }

    if (!loadSingleClickOpen()) {
        return false;
    }

    return true;
}

bool VNoteManagementTab::saveConfiguration()
{
    if (!saveImageFolder()) {
        return false;
    }

    if (!saveAttachmentFolder()) {
        return false;
    }

    if (!saveImageFolderExt()) {
        return false;
    }

    if (!saveSingleClickOpen()) {
        return false;
    }

    return true;
}

bool VNoteManagementTab::loadImageFolder()
{
    bool isCustom = g_config->isCustomImageFolder();

    m_customImageFolder->setChecked(isCustom);
    m_imageFolderEdit->setText(g_config->getImageFolder());
    m_imageFolderEdit->setEnabled(isCustom);

    return true;
}

bool VNoteManagementTab::saveImageFolder()
{
    if (m_customImageFolder->isChecked()) {
        g_config->setImageFolder(m_imageFolderEdit->text());
    } else {
        g_config->setImageFolder("");
    }

    return true;
}

void VNoteManagementTab::customImageFolderChanged(int p_state)
{
    if (p_state == Qt::Checked) {
        m_imageFolderEdit->setEnabled(true);
        m_imageFolderEdit->selectAll();
        m_imageFolderEdit->setFocus();
    } else {
        m_imageFolderEdit->setEnabled(false);
    }
}

bool VNoteManagementTab::loadAttachmentFolder()
{
    bool isCustom = g_config->isCustomAttachmentFolder();

    m_customAttachmentFolder->setChecked(isCustom);
    m_attachmentFolderEdit->setText(g_config->getAttachmentFolder());
    m_attachmentFolderEdit->setEnabled(isCustom);

    return true;
}

bool VNoteManagementTab::saveAttachmentFolder()
{
    if (m_customAttachmentFolder->isChecked()) {
        g_config->setAttachmentFolder(m_attachmentFolderEdit->text());
    } else {
        g_config->setAttachmentFolder("");
    }

    return true;
}

void VNoteManagementTab::customAttachmentFolderChanged(int p_state)
{
    if (p_state == Qt::Checked) {
        m_attachmentFolderEdit->setEnabled(true);
        m_attachmentFolderEdit->selectAll();
        m_attachmentFolderEdit->setFocus();
    } else {
        m_attachmentFolderEdit->setEnabled(false);
    }
}

bool VNoteManagementTab::loadImageFolderExt()
{
    bool isCustom = g_config->isCustomImageFolderExt();

    m_customImageFolderExt->setChecked(isCustom);
    m_imageFolderEditExt->setText(g_config->getImageFolderExt());
    m_imageFolderEditExt->setEnabled(isCustom);

    return true;
}

bool VNoteManagementTab::saveImageFolderExt()
{
    if (m_customImageFolderExt->isChecked()) {
        g_config->setImageFolderExt(m_imageFolderEditExt->text());
    } else {
        g_config->setImageFolderExt("");
    }

    return true;
}

void VNoteManagementTab::customImageFolderExtChanged(int p_state)
{
    if (p_state == Qt::Checked) {
        m_imageFolderEditExt->setEnabled(true);
        m_imageFolderEditExt->selectAll();
        m_imageFolderEditExt->setFocus();
    } else {
        m_imageFolderEditExt->setEnabled(false);
    }
}

bool VNoteManagementTab::loadSingleClickOpen()
{
    m_singleClickOpen->setChecked(g_config->getSingleClickClosePreviousTab());
    return true;
}

bool VNoteManagementTab::saveSingleClickOpen()
{
    g_config->setSingleClickClosePreviousTab(m_singleClickOpen->isChecked());
    return true;
}

VMarkdownTab::VMarkdownTab(QWidget *p_parent)
    : QWidget(p_parent)
{
    // Default note open mode.
    m_openModeCombo = VUtils::getComboBox();
    m_openModeCombo->setToolTip(tr("Default mode to open a file"));
    m_openModeCombo->addItem(tr("Read Mode"), (int)OpenFileMode::Read);
    m_openModeCombo->addItem(tr("Edit Mode"), (int)OpenFileMode::Edit);

    // Heading sequence.
    m_headingSequenceTypeCombo = VUtils::getComboBox();
    m_headingSequenceTypeCombo->setToolTip(tr("Enable auto sequence for all headings (in the form like 1.2.3.4.)"));
    m_headingSequenceTypeCombo->addItem(tr("Disabled"), (int)HeadingSequenceType::Disabled);
    m_headingSequenceTypeCombo->addItem(tr("Enabled"), (int)HeadingSequenceType::Enabled);
    m_headingSequenceTypeCombo->addItem(tr("Enabled for intrenal notes only"), (int)HeadingSequenceType::EnabledNoteOnly);

    m_headingSequenceLevelCombo = VUtils::getComboBox();
    m_headingSequenceLevelCombo->setToolTip(tr("Base level to start heading sequence"));
    m_headingSequenceLevelCombo->addItem(tr("1"), 1);
    m_headingSequenceLevelCombo->addItem(tr("2"), 2);
    m_headingSequenceLevelCombo->addItem(tr("3"), 3);
    m_headingSequenceLevelCombo->addItem(tr("4"), 4);
    m_headingSequenceLevelCombo->addItem(tr("5"), 5);
    m_headingSequenceLevelCombo->addItem(tr("6"), 6);
    m_headingSequenceLevelCombo->setEnabled(false);

    connect(m_headingSequenceTypeCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
            this, [this](int p_index){
                if (p_index > -1) {
                    HeadingSequenceType type = (HeadingSequenceType)m_headingSequenceTypeCombo->itemData(p_index).toInt();
                    m_headingSequenceLevelCombo->setEnabled(type != HeadingSequenceType::Disabled);
                }
            });

    QHBoxLayout *headingSequenceLayout = new QHBoxLayout();
    headingSequenceLayout->addWidget(m_headingSequenceTypeCombo);
    headingSequenceLayout->addWidget(m_headingSequenceLevelCombo);

    // Color column.
    m_colorColumnEdit = new VLineEdit();
    m_colorColumnEdit->setToolTip(tr("Specify the screen column in fenced code block "
                                     "which will be highlighted"));
    QValidator *validator = new QRegExpValidator(QRegExp("\\d+"), this);
    m_colorColumnEdit->setValidator(validator);

    QLabel *colorColumnLabel = new QLabel(tr("Color column:"));
    colorColumnLabel->setToolTip(m_colorColumnEdit->toolTip());

    // MathJax.
    m_mathjaxConfigEdit = new VLineEdit();
    m_mathjaxConfigEdit->setToolTip(tr("Location of MathJax JavaScript and its configuration "
                                       "(restart VNote to make it work in in-place preview)"));

    // PlantUML.
    m_plantUMLModeCombo = VUtils::getComboBox();
    m_plantUMLModeCombo->setToolTip(tr("Enable PlantUML support in Markdown"));
    m_plantUMLModeCombo->addItem(tr("Disabled"), PlantUMLMode::DisablePlantUML);
    m_plantUMLModeCombo->addItem(tr("Online Service"), PlantUMLMode::OnlinePlantUML);
    m_plantUMLModeCombo->addItem(tr("Local JAR"), PlantUMLMode::LocalPlantUML);

    m_plantUMLServerEdit = new VLineEdit();
    m_plantUMLServerEdit->setToolTip(tr("Server address for online PlantUML"));

    m_plantUMLJarEdit = new VLineEdit();
    m_plantUMLJarEdit->setToolTip(tr("Location to the PlantUML JAR executable for local PlantUML"));

    // Graphviz.
    m_graphvizCB = new QCheckBox(tr("Graphviz"));
    m_graphvizCB->setToolTip(tr("Enable Graphviz for drawing graph"));

    m_graphvizDotEdit = new VLineEdit();
    m_graphvizDotEdit->setPlaceholderText(tr("Empty to detect automatically"));
    m_graphvizDotEdit->setToolTip(tr("Location to the GraphViz dot executable"));

    QFormLayout *mainLayout = new QFormLayout();
    mainLayout->addRow(tr("Open mode:"), m_openModeCombo);
    mainLayout->addRow(tr("Heading sequence:"), headingSequenceLayout);
    mainLayout->addRow(colorColumnLabel, m_colorColumnEdit);
    mainLayout->addRow(tr("MathJax configuration:"), m_mathjaxConfigEdit);
    mainLayout->addRow(tr("PlantUML:"), m_plantUMLModeCombo);
    mainLayout->addRow(tr("PlantUML server:"), m_plantUMLServerEdit);
    mainLayout->addRow(tr("PlantUML JAR:"), m_plantUMLJarEdit);
    mainLayout->addRow(m_graphvizCB);
    mainLayout->addRow(tr("Graphviz executable:"), m_graphvizDotEdit);

    setLayout(mainLayout);
}

bool VMarkdownTab::loadConfiguration()
{
    if (!loadOpenMode()) {
        return false;
    }

    if (!loadHeadingSequence()) {
        return false;
    }

    if (!loadColorColumn()) {
        return false;
    }

    if (!loadMathJax()) {
        return false;
    }

    if (!loadPlantUML()) {
        return false;
    }

    if (!loadGraphviz()) {
        return false;
    }

    return true;
}

bool VMarkdownTab::saveConfiguration()
{
    if (!saveOpenMode()) {
        return false;
    }

    if (!saveHeadingSequence()) {
        return false;
    }

    if (!saveColorColumn()) {
        return false;
    }

    if (!saveMathJax()) {
        return false;
    }

    if (!savePlantUML()) {
        return false;
    }

    if (!saveGraphviz()) {
        return false;
    }

    return true;
}

bool VMarkdownTab::loadOpenMode()
{
    int mode = (int)g_config->getNoteOpenMode();
    bool found = false;
    for (int i = 0; i < m_openModeCombo->count(); ++i) {
        if (m_openModeCombo->itemData(i).toInt() == mode) {
            m_openModeCombo->setCurrentIndex(i);
            found = true;
            break;
        }
    }

    Q_ASSERT(found);
    return true;
}

bool VMarkdownTab::saveOpenMode()
{
    int mode = m_openModeCombo->currentData().toInt();
    g_config->setNoteOpenMode((OpenFileMode)mode);
    return true;
}

bool VMarkdownTab::loadHeadingSequence()
{
    HeadingSequenceType type = g_config->getHeadingSequenceType();
    int level = g_config->getHeadingSequenceBaseLevel();
    if (level < 1 || level > 6) {
        level = 1;
    }

    int idx = m_headingSequenceTypeCombo->findData((int)type);
    Q_ASSERT(idx > -1);
    m_headingSequenceTypeCombo->setCurrentIndex(idx);
    m_headingSequenceLevelCombo->setCurrentIndex(level - 1);
    m_headingSequenceLevelCombo->setEnabled(type != HeadingSequenceType::Disabled);

    return true;
}

bool VMarkdownTab::saveHeadingSequence()
{
    QVariant typeData = m_headingSequenceTypeCombo->currentData();
    Q_ASSERT(typeData.isValid());
    g_config->setHeadingSequenceType((HeadingSequenceType)typeData.toInt());
    g_config->setHeadingSequenceBaseLevel(m_headingSequenceLevelCombo->currentData().toInt());

    return true;
}

bool VMarkdownTab::loadColorColumn()
{
    int colorColumn = g_config->getColorColumn();
    m_colorColumnEdit->setText(QString::number(colorColumn <= 0 ? 0 : colorColumn));
    return true;
}

bool VMarkdownTab::saveColorColumn()
{
    bool ok = false;
    int colorColumn = m_colorColumnEdit->text().toInt(&ok);
    if (ok && colorColumn >= 0) {
        g_config->setColorColumn(colorColumn);
    }

    return true;
}

bool VMarkdownTab::loadMathJax()
{
    m_mathjaxConfigEdit->setText(g_config->getMathjaxJavascript());
    return true;
}

bool VMarkdownTab::saveMathJax()
{
    g_config->setMathjaxJavascript(m_mathjaxConfigEdit->text());
    return true;
}

bool VMarkdownTab::loadPlantUML()
{
    m_plantUMLModeCombo->setCurrentIndex(m_plantUMLModeCombo->findData(g_config->getPlantUMLMode()));
    m_plantUMLServerEdit->setText(g_config->getPlantUMLServer());
    m_plantUMLJarEdit->setText(g_config->getPlantUMLJar());
    return true;
}

bool VMarkdownTab::savePlantUML()
{
    g_config->setPlantUMLMode(m_plantUMLModeCombo->currentData().toInt());
    g_config->setPlantUMLServer(m_plantUMLServerEdit->text());
    g_config->setPlantUMLJar(m_plantUMLJarEdit->text());
    return true;
}

bool VMarkdownTab::loadGraphviz()
{
    m_graphvizCB->setChecked(g_config->getEnableGraphviz());
    m_graphvizDotEdit->setText(g_config->getGraphvizDot());
    return true;
}

bool VMarkdownTab::saveGraphviz()
{
    g_config->setEnableGraphviz(m_graphvizCB->isChecked());
    g_config->setGraphvizDot(m_graphvizDotEdit->text());
    return true;
}
