#include "vconfirmdeletiondialog.h"

#include <QtWidgets>

#include "utils/vutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

ConfirmItemWidget::ConfirmItemWidget(bool p_checked,
                                     const QString &p_file,
                                     const QString &p_tip,
                                     int p_index,
                                     QWidget *p_parent)
    : QWidget(p_parent), m_index(p_index)
{
    setupUI();

    m_checkBox->setChecked(p_checked);
    m_fileLabel->setText(p_file);
    if (!p_tip.isEmpty()) {
        m_fileLabel->setToolTip(p_tip);
    }
}

void ConfirmItemWidget::setupUI()
{
    m_checkBox = new QCheckBox;
    connect(m_checkBox, &QCheckBox::stateChanged,
            this, &ConfirmItemWidget::checkStateChanged);

    m_fileLabel = new QLabel;
    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->addWidget(m_checkBox);
    mainLayout->addWidget(m_fileLabel);
    mainLayout->addStretch();
    mainLayout->setContentsMargins(3, 0, 0, 0);

    setLayout(mainLayout);
}

bool ConfirmItemWidget::isChecked() const
{
    return m_checkBox->isChecked();
}

int ConfirmItemWidget::getIndex() const
{
    return m_index;
}

VConfirmDeletionDialog::VConfirmDeletionDialog(const QString &p_title,
                                               const QString &p_text,
                                               const QString &p_info,
                                               const QVector<ConfirmItemInfo> &p_items,
                                               bool p_enableAskAgain,
                                               bool p_askAgainEnabled,
                                               bool p_enablePreview,
                                               QWidget *p_parent)
    : QDialog(p_parent),
      m_enableAskAgain(p_enableAskAgain),
      m_askAgainEnabled(p_askAgainEnabled),
      m_enablePreview(p_enablePreview),
      m_items(p_items)
{
    setupUI(p_title, p_text, p_info);

    initItems();

    updateCountLabel();
}

void VConfirmDeletionDialog::setupUI(const QString &p_title,
                                     const QString &p_text,
                                     const QString &p_info)
{
    QLabel *textLabel = NULL;
    if (!p_text.isEmpty()) {
        textLabel = new QLabel(p_text);
        textLabel->setWordWrap(true);
    }

    QLabel *infoLabel = NULL;
    if (!p_info.isEmpty()) {
        infoLabel = new QLabel(p_info);
        infoLabel->setWordWrap(true);
    }

    m_countLabel = new QLabel("Items");
    QHBoxLayout *labelLayout = new QHBoxLayout;
    labelLayout->addWidget(m_countLabel);
    labelLayout->addStretch();
    labelLayout->setContentsMargins(0, 0, 0, 0);

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
    if (textLabel) {
        mainLayout->addWidget(textLabel);
    }

    if (infoLabel) {
        mainLayout->addWidget(infoLabel);
    }

    mainLayout->addWidget(m_askAgainCB);
    mainLayout->addWidget(m_btnBox);
    mainLayout->addLayout(labelLayout);
    mainLayout->addLayout(midLayout);

    setLayout(mainLayout);
    setWindowTitle(p_title);
}

QVector<ConfirmItemInfo> VConfirmDeletionDialog::getConfirmedItems() const
{
    QVector<ConfirmItemInfo> confirmedItems;

    for (int i = 0; i < m_listWidget->count(); ++i) {
        ConfirmItemWidget *widget = getItemWidget(m_listWidget->item(i));
        if (widget->isChecked()) {
            confirmedItems.push_back(m_items[widget->getIndex()]);
        }
    }

    return confirmedItems;
}

void VConfirmDeletionDialog::initItems()
{
    m_listWidget->clear();

    for (int i = 0; i < m_items.size(); ++i) {
        ConfirmItemWidget *itemWidget = new ConfirmItemWidget(true,
                                                              m_items[i].m_name,
                                                              m_items[i].m_tip,
                                                              i,
                                                              this);
        connect(itemWidget, &ConfirmItemWidget::checkStateChanged,
                this, &VConfirmDeletionDialog::updateCountLabel);

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
            int idx = widget->getIndex();
            Q_ASSERT(idx < m_items.size());
            QPixmap image(m_items[idx].m_path);
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

void VConfirmDeletionDialog::updateCountLabel()
{
    int total = m_listWidget->count();
    int checked = 0;

    for (int i = 0; i < total; ++i) {
        ConfirmItemWidget *widget = getItemWidget(m_listWidget->item(i));
        if (widget->isChecked()) {
            ++checked;
        }
    }

    m_countLabel->setText(tr("%1/%2 Items").arg(checked).arg(total));
}
