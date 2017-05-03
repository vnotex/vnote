#include <QtWidgets>
#include "vdeletenotebookdialog.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

VDeleteNotebookDialog::VDeleteNotebookDialog(const QString &p_title, const QString &p_name,
                                             const QString &p_path, QWidget *p_parent)
    : QDialog(p_parent), m_path(p_path)
{
    setupUI(p_title, p_name);
}

void VDeleteNotebookDialog::setupUI(const QString &p_title, const QString &p_name)
{
    QLabel *infoLabel = new QLabel(tr("Are you sure to delete notebook <span style=\"%1\">%2</span>?")
                                     .arg(vconfig.c_dataTextStyle).arg(p_name));
    m_warningLabel = new QLabel();
    m_warningLabel->setWordWrap(true);

    m_notDeleteCheck = new QCheckBox(tr("Do not delete files from disk."), this);
    m_notDeleteCheck->setChecked(true);
    m_notDeleteCheck->setToolTip(tr("When checked, VNote just removes the notebook instead of deleting files from disk"));
    connect(m_notDeleteCheck, &QCheckBox::stateChanged, this, &VDeleteNotebookDialog::notDeleteCheckChanged);

    notDeleteCheckChanged(true);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = m_btnBox->button(QDialogButtonBox::Ok);
    okBtn->setProperty("DangerBtn", true);

    // Standard Warning icon.
    QLabel *iconLabel = new QLabel();
    QPixmap pixmap = standardIcon(QMessageBox::Warning);
    if (pixmap.isNull()) {
        iconLabel->hide();
    } else {
        iconLabel->setPixmap(pixmap);
    }

    QVBoxLayout *iconLayout = new QVBoxLayout();
    iconLayout->addStretch();
    iconLayout->addWidget(iconLabel);
    iconLayout->addStretch();

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->addWidget(infoLabel);
    infoLayout->addWidget(m_notDeleteCheck);
    infoLayout->addWidget(m_warningLabel);

    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addLayout(iconLayout);
    topLayout->addLayout(infoLayout);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_btnBox);

    setLayout(mainLayout);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    setWindowTitle(p_title);
}

bool VDeleteNotebookDialog::getDeleteFiles() const
{
    return !m_notDeleteCheck->isChecked();
}

QPixmap VDeleteNotebookDialog::standardIcon(QMessageBox::Icon p_icon)
{
    QStyle *style = this->style();
    int iconSize = style->pixelMetric(QStyle::PM_MessageBoxIconSize, 0, this);
    QIcon tmpIcon;
    switch (p_icon) {
    case QMessageBox::Information:
        tmpIcon = style->standardIcon(QStyle::SP_MessageBoxInformation, 0, this);
        break;
    case QMessageBox::Warning:
        tmpIcon = style->standardIcon(QStyle::SP_MessageBoxWarning, 0, this);
        break;
    case QMessageBox::Critical:
        tmpIcon = style->standardIcon(QStyle::SP_MessageBoxCritical, 0, this);
        break;
    case QMessageBox::Question:
        tmpIcon = style->standardIcon(QStyle::SP_MessageBoxQuestion, 0, this);
        break;
    default:
        break;
    }

    if (!tmpIcon.isNull()) {
        QWindow *window = this->windowHandle();
        if (!window) {
            if (const QWidget *nativeParent = this->nativeParentWidget()) {
                window = nativeParent->windowHandle();
            }
        }
        return tmpIcon.pixmap(window, QSize(iconSize, iconSize));
    }
    return QPixmap();
}

void VDeleteNotebookDialog::notDeleteCheckChanged(int p_state)
{
    if (p_state) {
        m_warningLabel->setText(tr("VNote won't delete files under directory <span style=\"%1\">%2</span>.")
                                  .arg(vconfig.c_dataTextStyle).arg(m_path));
    } else {
        m_warningLabel->setText(tr("<span style=\"%1\">WARNING</span>: "
                                   "VNote may delete <b>ANY</b> files under directory <span style=\"%2\">%3</span>! "
                                   "VNote will try to delete all the root directories within this notebook one by one. "
                                   "It may be UNRECOVERABLE!")
                                  .arg(vconfig.c_warningTextStyle).arg(vconfig.c_dataTextStyle).arg(m_path));
    }
}
