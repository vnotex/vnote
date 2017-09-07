#include <QtWidgets>
#include "vnewfiledialog.h"
#include "vconfigmanager.h"
#include "vdirectory.h"

extern VConfigManager *g_config;

VNewFileDialog::VNewFileDialog(const QString &title, const QString &info,
                               const QString &defaultName, VDirectory *directory,
                               QWidget *parent)
    : QDialog(parent), title(title), info(info),
      defaultName(defaultName), m_directory(directory)
{
    setupUI();

    connect(nameEdit, &QLineEdit::textChanged, this, &VNewFileDialog::handleInputChanged);

    handleInputChanged();
}

void VNewFileDialog::setupUI()
{
    QLabel *infoLabel = NULL;
    if (!info.isEmpty()) {
        infoLabel = new QLabel(info);
    }

    // Name.
    QLabel *nameLabel = new QLabel(tr("Note &name:"));
    nameEdit = new QLineEdit(defaultName);
    int dotIndex = defaultName.lastIndexOf('.');
    nameEdit->setSelection(0, (dotIndex == -1) ? defaultName.size() : dotIndex);
    nameLabel->setBuddy(nameEdit);

    // InsertTitle.
    m_insertTitleCB = new QCheckBox(tr("Insert note name as title (for Markdown only)"));
    m_insertTitleCB->setToolTip(tr("Insert note name into the new note as a title"));
    m_insertTitleCB->setChecked(g_config->getInsertTitleFromNoteName());
    connect(m_insertTitleCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                g_config->setInsertTitleFromNoteName(p_state == Qt::Checked);
            });

    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(nameLabel, nameEdit);
    topLayout->addRow("", m_insertTitleCB);

    m_warnLabel = new QLabel();
    m_warnLabel->setWordWrap(true);
    m_warnLabel->hide();

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    nameEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_warnLabel);
    mainLayout->addWidget(m_btnBox);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(mainLayout);

    setWindowTitle(title);
}

void VNewFileDialog::handleInputChanged()
{
    bool showWarnLabel = false;
    QString name = nameEdit->text();
    bool nameOk = !name.isEmpty();
    if (nameOk) {
        // Check if the name conflicts with existing notebook name.
        // Case-insensitive when creating note.
        if (m_directory->findFile(name, false)) {
            nameOk = false;
            showWarnLabel = true;
            QString nameConflictText = tr("<span style=\"%1\">WARNING</span>: Name (case-insensitive) already exists. "
                                          "Please choose another name.")
                                          .arg(g_config->c_warningTextStyle);
            m_warnLabel->setText(nameConflictText);
        }
    }

    m_warnLabel->setVisible(showWarnLabel);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(nameOk);
}

QString VNewFileDialog::getNameInput() const
{
    return nameEdit->text();
}

bool VNewFileDialog::getInsertTitleInput() const
{
    return m_insertTitleCB->isChecked();
}
