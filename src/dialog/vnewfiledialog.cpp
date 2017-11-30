#include <QtWidgets>
#include "vnewfiledialog.h"
#include "vconfigmanager.h"
#include "vdirectory.h"
#include "vlineedit.h"
#include "utils/vutils.h"
#include "utils/vmetawordmanager.h"

extern VConfigManager *g_config;

extern VMetaWordManager *g_mwMgr;

QString VNewFileDialog::s_lastTemplateFile;


VNewFileDialog::VNewFileDialog(const QString &p_title,
                               const QString &p_info,
                               const QString &p_defaultName,
                               VDirectory *p_directory,
                               QWidget *p_parent)
    : QDialog(p_parent),
      m_currentTemplateType(DocType::Unknown),
      m_directory(p_directory)
{
    setupUI(p_title, p_info, p_defaultName);

    connect(m_nameEdit, &VLineEdit::textChanged, this, &VNewFileDialog::handleInputChanged);

    connect(m_templateCB, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &VNewFileDialog::handleCurrentTemplateChanged);

    handleInputChanged();

    tryToSelectLastTemplate();
}

void VNewFileDialog::setupUI(const QString &p_title,
                             const QString &p_info,
                             const QString &p_defaultName)
{
    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
    }

    // Name.
    m_nameEdit = new VLineEdit(p_defaultName);
    QValidator *validator = new QRegExpValidator(QRegExp(VUtils::c_fileNameRegExp),
                                                 m_nameEdit);
    m_nameEdit->setValidator(validator);
    int dotIndex = p_defaultName.lastIndexOf('.');
    m_nameEdit->setSelection(0, (dotIndex == -1) ? p_defaultName.size() : dotIndex);

    // Template.
    m_templateCB = VUtils::getComboBox();
    m_templateCB->setToolTip(tr("Choose a template (magic word supported)"));
    m_templateCB->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    QPushButton *templateBtn = new QPushButton(QIcon(":/resources/icons/manage_template.svg"),
                                                 "");
    templateBtn->setToolTip(tr("Manage Templates"));
    templateBtn->setProperty("FlatBtn", true);
    connect(templateBtn, &QPushButton::clicked,
            this, [this]() {
                QUrl url = QUrl::fromLocalFile(g_config->getTemplateConfigFolder());
                QDesktopServices::openUrl(url);
            });

    QHBoxLayout *tempBtnLayout = new QHBoxLayout();
    tempBtnLayout->addWidget(m_templateCB);
    tempBtnLayout->addWidget(templateBtn);
    tempBtnLayout->addStretch();

    m_templateEdit = new QTextEdit();
    m_templateEdit->setReadOnly(true);

    QVBoxLayout *templateLayout = new QVBoxLayout();
    templateLayout->addLayout(tempBtnLayout);
    templateLayout->addWidget(m_templateEdit);

    m_templateEdit->hide();

    // InsertTitle.
    m_insertTitleCB = new QCheckBox(tr("Insert note name as title (for Markdown only)"));
    m_insertTitleCB->setToolTip(tr("Insert note name into the new note as a title"));
    m_insertTitleCB->setChecked(g_config->getInsertTitleFromNoteName());
    connect(m_insertTitleCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                g_config->setInsertTitleFromNoteName(p_state == Qt::Checked);
            });

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(tr("Note &name:"), m_nameEdit);
    topLayout->addWidget(m_insertTitleCB);
    topLayout->addRow(tr("Template:"), templateLayout);

    m_nameEdit->setMinimumWidth(m_insertTitleCB->sizeHint().width());

    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted,
            this, [this]() {
                s_lastTemplateFile = m_templateCB->currentData().toString();
                QDialog::accept();
            });
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    m_templateCB->setMaximumWidth(okBtn->sizeHint().width() * 4);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(mainLayout);

    setWindowTitle(p_title);
}

void VNewFileDialog::handleInputChanged()
{
    bool showWarnLabel = false;
    const QString name = m_nameEdit->getEvaluatedText();
    bool nameOk = !name.isEmpty();
    if (nameOk) {
        // Check if the name conflicts with existing note name.
        // Case-insensitive when creating note.
        QString warnText;
        if (m_directory->findFile(name, false)) {
            nameOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Name (case-insensitive) <span style=\"%2\">%3</span> already exists. "
                          "Please choose another name.")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(name);
        } else if (!VUtils::checkFileNameLegal(name)) {
            // Check if evaluated name contains illegal characters.
            nameOk = false;
            warnText = tr("<span style=\"%1\">WARNING</span>: "
                          "Name <span style=\"%2\">%3</span> contains illegal characters "
                          "(after magic word evaluation).")
                         .arg(g_config->c_warningTextStyle)
                         .arg(g_config->c_dataTextStyle)
                         .arg(name);
        }

        if (!nameOk) {
            showWarnLabel = true;
            m_warnLabel->setText(warnText);
        }
    }

    m_warnLabel->setVisible(showWarnLabel);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(nameOk);

    if (nameOk) {
        updateTemplates(VUtils::docTypeFromName(name));
    }
}

QString VNewFileDialog::getNameInput() const
{
    return m_nameEdit->getEvaluatedText();
}

bool VNewFileDialog::getInsertTitleInput() const
{
    return m_insertTitleCB->isEnabled() && m_insertTitleCB->isChecked();
}

void VNewFileDialog::updateTemplates(DocType p_type)
{
    if (m_currentTemplateType == p_type) {
        return;
    }

    m_currentTemplateType = p_type;

    // Clear the combo.
    m_templateCB->clear();

    // Add None item.
    m_templateCB->addItem(tr("None"), "None");

    if (m_currentTemplateType == DocType::Unknown) {
        return;
    }

    int idx = 1;
    auto templates = g_config->getNoteTemplates(m_currentTemplateType);
    for (auto const & tp : templates) {
        m_templateCB->addItem(tp, tp);
        m_templateCB->setItemData(idx++, tp, Qt::ToolTipRole);
    }
}

void VNewFileDialog::handleCurrentTemplateChanged(int p_idx)
{
    if (p_idx == -1) {
        m_templateEdit->hide();
        enableInsertTitleCB(false);
        return;
    }

    QString file = m_templateCB->itemData(p_idx).toString();
    if (file == "None") {
        m_templateEdit->hide();
        enableInsertTitleCB(false);
        return;
    }

    // Read the template file.
    QString filePath = QDir(g_config->getTemplateConfigFolder()).filePath(file);
    m_template = VUtils::readFileFromDisk(filePath);
    DocType type = VUtils::docTypeFromName(file);
    switch (type) {
    case DocType::Html:
        m_templateEdit->setHtml(m_template);
        break;

    case DocType::Markdown:
        m_templateEdit->setPlainText(m_template);
        break;

    default:
        m_templateEdit->setPlainText(m_template);
        break;
    }

    m_templateEdit->show();
    enableInsertTitleCB(true);
}

void VNewFileDialog::enableInsertTitleCB(bool p_hasTemplate)
{
    m_insertTitleCB->setEnabled(!p_hasTemplate
                                && VUtils::docTypeFromName(m_nameEdit->getEvaluatedText())
                                   == DocType::Markdown);
}

bool VNewFileDialog::isTemplateUsed() const
{
    QString file = m_templateCB->currentData().toString();
    return !(file.isEmpty() || file == "None");
}

QString VNewFileDialog::getTemplate() const
{
    QString name = m_nameEdit->getEvaluatedText();
    QHash<QString, QString> overriddenTable;
    overriddenTable.insert("note", name);
    overriddenTable.insert("no", QFileInfo(name).completeBaseName());
    return g_mwMgr->evaluate(m_template, overriddenTable);
}

void VNewFileDialog::tryToSelectLastTemplate()
{
    Q_ASSERT(m_templateCB->count() > 0
             && m_templateCB->itemData(0).toString() == "None");
    if (s_lastTemplateFile.isEmpty() || s_lastTemplateFile == "None") {
        m_templateCB->setCurrentIndex(0);
        return;
    }

    int idx = m_templateCB->findData(s_lastTemplateFile);
    if (idx != -1) {
        m_templateCB->setCurrentIndex(idx);
    } else {
        s_lastTemplateFile.clear();
    }
}
