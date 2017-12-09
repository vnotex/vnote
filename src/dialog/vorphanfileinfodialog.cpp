#include "vorphanfileinfodialog.h"

#include <QtWidgets>
#include "vorphanfile.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

VOrphanFileInfoDialog::VOrphanFileInfoDialog(const VOrphanFile *p_file, QWidget *p_parent)
    : QDialog(p_parent), m_file(p_file)
{
    setupUI();

    connect(m_imageFolderEdit, &QLineEdit::textChanged,
            this, &VOrphanFileInfoDialog::handleInputChanged);

    handleInputChanged();
}

void VOrphanFileInfoDialog::setupUI()
{
    QFormLayout *topLayout = new QFormLayout();

    QLabel *fileLabel = new QLabel(m_file->fetchPath());
    topLayout->addRow(tr("File:"), fileLabel);

    m_imageFolderEdit = new QLineEdit(m_file->getImageFolder());
    m_imageFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                            .arg(g_config->getImageFolderExt()));
    QString imgFolderTip = tr("Set the path of the image folder to store images "
                              "of this file.\nIf absolute path is used, "
                              "VNote will not manage those images."
                              "(empty to use global configuration)");
    m_imageFolderEdit->setToolTip(imgFolderTip);
    topLayout->addRow(tr("&Image folder:"), m_imageFolderEdit);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setProperty("SpecialBtn", true);
    m_imageFolderEdit->setMinimumWidth(okBtn->sizeHint().width() * 3);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    setLayout(mainLayout);
    setWindowTitle(tr("External File Information"));
}

QString VOrphanFileInfoDialog::getImageFolder() const
{
    return m_imageFolderEdit->text();
}

void VOrphanFileInfoDialog::handleInputChanged()
{
    bool ok = false;
    QString imgFolder = m_imageFolderEdit->text();
    if (imgFolder.isEmpty() || VUtils::checkPathLegal(imgFolder)) {
        ok = true;
    }

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setEnabled(ok);
}
