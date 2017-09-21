#include "vconfirmdeletiondialog.h"

#include <QtWidgets>

#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

class ConfirmItemWidget : public QWidget
{
public:
    explicit ConfirmItemWidget(QWidget *p_parent = NULL)
        : QWidget(p_parent)
    {
        setupUI();
    }

    ConfirmItemWidget(bool p_checked, const QString &p_file, QWidget *p_parent = NULL)
        : QWidget(p_parent)
    {
        setupUI();

        m_checkBox->setChecked(p_checked);
        m_fileLabel->setText(p_file);
    }

    bool isChecked() const
    {
        return m_checkBox->isChecked();
    }

    QString getFile() const
    {
        return m_fileLabel->text();
    }

private:
    void setupUI()
    {
        m_checkBox = new QCheckBox;
        m_fileLabel = new QLabel;
        QHBoxLayout *mainLayout = new QHBoxLayout;
        mainLayout->addWidget(m_checkBox);
        mainLayout->addWidget(m_fileLabel);
        mainLayout->addStretch();
        mainLayout->setContentsMargins(3, 0, 0, 0);

        setLayout(mainLayout);
    }

    QCheckBox *m_checkBox;
    QLabel *m_fileLabel;
};

VConfirmDeletionDialog::VConfirmDeletionDialog(const QString &p_title,
                                               const QString &p_info,
                                               const QVector<QString> &p_files,
                                               bool p_enableAskAgain,
                                               bool p_askAgainEnabled,
                                               bool p_enablePreview,
                                               QWidget *p_parent)
    : QDialog(p_parent),
      m_enableAskAgain(p_enableAskAgain),
      m_askAgainEnabled(p_askAgainEnabled),
      m_enablePreview(p_enablePreview)
{
    setupUI(p_title, p_info);

    initFileItems(p_files);
}

void VConfirmDeletionDialog::setupUI(const QString &p_title, const QString &p_info)
{
    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
        infoLabel->setWordWrap(true);
    }

    m_listWidget = new QListWidget();
    connect(m_listWidget, &QListWidget::currentRowChanged,
            this, &VConfirmDeletionDialog::currentFileChanged);

    m_previewer = new QLabel();

    m_askAgainCB = new QCheckBox(tr("Do not ask for confirmation again"));
    m_askAgainCB->setChecked(!m_askAgainEnabled);
    m_askAgainCB->setVisible(m_enableAskAgain);

    // Ok is the default button.
    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_btnBox->button(QDialogButtonBox::Ok)->setStyleSheet(g_config->c_dangerBtnStyle);

    QHBoxLayout *midLayout = new QHBoxLayout;
    midLayout->addWidget(m_listWidget);
    if (m_enablePreview) {
        midLayout->addStretch();
        midLayout->addWidget(m_previewer);
        midLayout->addStretch();
    } else {
        midLayout->addWidget(m_previewer);
        m_previewer->setVisible(false);
    }

    QVBoxLayout *mainLayout = new QVBoxLayout;
    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addWidget(m_askAgainCB);
    mainLayout->addWidget(m_btnBox);
    mainLayout->addLayout(midLayout);

    setLayout(mainLayout);
    setWindowTitle(p_title);
}

QVector<QString> VConfirmDeletionDialog::getConfirmedFiles() const
{
    QVector<QString> files;

    for (int i = 0; i < m_listWidget->count(); ++i) {
        ConfirmItemWidget *widget = getItemWidget(m_listWidget->item(i));
        if (widget->isChecked()) {
            files.push_back(widget->getFile());
        }
    }

    return files;
}

void VConfirmDeletionDialog::initFileItems(const QVector<QString> &p_files)
{
    m_listWidget->clear();

    for (int i = 0; i < p_files.size(); ++i) {
        ConfirmItemWidget *itemWidget = new ConfirmItemWidget(true,
                                                              p_files[i],
                                                              this);
        QListWidgetItem *item = new QListWidgetItem();
        QSize size = itemWidget->sizeHint();
        size.setHeight(size.height() * 2);
        item->setSizeHint(size);

        m_listWidget->addItem(item);
        m_listWidget->setItemWidget(item, itemWidget);
    }

    m_listWidget->setMinimumSize(m_listWidget->sizeHint());
    m_listWidget->setCurrentRow(-1);
    m_listWidget->setCurrentRow(0);
}

bool VConfirmDeletionDialog::getAskAgainEnabled() const
{
    return !m_askAgainCB->isChecked();
}

void VConfirmDeletionDialog::currentFileChanged(int p_row)
{
    bool succeed = false;
    if (p_row > -1 && m_enablePreview) {
        ConfirmItemWidget *widget = getItemWidget(m_listWidget->item(p_row));
        if (widget) {
            QPixmap image(widget->getFile());
            if (!image.isNull()) {
                int width = 512 * VUtils::calculateScaleFactor();
                QSize previewSize(width, width);
                m_previewer->setPixmap(image.scaled(previewSize));
                succeed = true;
            }
        }
    }

    m_previewer->setVisible(succeed);
}

ConfirmItemWidget *VConfirmDeletionDialog::getItemWidget(QListWidgetItem *p_item) const
{
    QWidget *wid = m_listWidget->itemWidget(p_item);
    return dynamic_cast<ConfirmItemWidget *>(wid);
}
