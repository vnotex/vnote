#include "deleteconfirmdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QCheckBox>
#include <QUrl>
#include <QScrollArea>

#include "global.h"
#include <utils/widgetutils.h>
#include "selectionitemwidget.h"

using namespace vnotex;

DeleteConfirmDialog::DeleteConfirmDialog(const QString &p_title,
                                         const QString &p_text,
                                         const QString &p_info,
                                         const QVector<ConfirmItemInfo> &p_items,
                                         DeleteConfirmDialog::Flags p_flags,
                                         bool p_noAskChecked,
                                         QWidget *p_parent)
    : ScrollDialog(p_parent),
      m_items(p_items)
{
    setupUI(p_title, p_text, p_info, p_flags, p_noAskChecked);

    updateItemsList();

    updateCountLabel();
}

void DeleteConfirmDialog::setupUI(const QString &p_title,
                                  const QString &p_text,
                                  const QString &p_info,
                                  DeleteConfirmDialog::Flags p_flags,
                                  bool p_noAskChecked)
{
    auto mainWidget = new QWidget(this);
    setCentralWidget(mainWidget);

    auto mainLayout = new QVBoxLayout(mainWidget);

    // Text.
    if (!p_text.isEmpty()) {
        auto textLabel = new QLabel(p_text, mainWidget);
        textLabel->setWordWrap(true);
        mainLayout->addWidget(textLabel);
    }

    // Info.
    if (!p_info.isEmpty()) {
        auto infoLabel = new QLabel(p_info, mainWidget);
        infoLabel->setWordWrap(true);
        mainLayout->addWidget(infoLabel);
    }

    // Ask again.
    if (p_flags & Flag::AskAgain) {
        m_noAskCB = new QCheckBox(tr("Do not ask again"), mainWidget);
        m_noAskCB->setChecked(p_noAskChecked);
        mainLayout->addWidget(m_noAskCB);
    }

    // Count.
    {
        QHBoxLayout *labelLayout = new QHBoxLayout();

        m_countLabel = new QLabel("Items", mainWidget);

        labelLayout->addWidget(m_countLabel);
        labelLayout->addStretch();
        labelLayout->setContentsMargins(0, 0, 0, 0);

        mainLayout->addLayout(labelLayout);
    }

    // List and preview.
    {
        auto listLayout = new QHBoxLayout();

        m_listWidget = new QListWidget(mainWidget);
        connect(m_listWidget, &QListWidget::currentRowChanged,
                this, &DeleteConfirmDialog::currentFileChanged);
        connect(m_listWidget, &QListWidget::itemActivated,
                this, [this](QListWidgetItem *p_item) {
                    if (!p_item) {
                        return;
                    }

                    auto widget = getItemWidget(p_item);
                    Q_ASSERT(widget);
                    QString filePath = m_items[widget->getData().toInt()].m_path;
                    if (!filePath.isEmpty()) {
                        WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(filePath));
                    }
                });

        listLayout->addWidget(m_listWidget);

        if (p_flags & Flag::Preview) {
            m_previewer = new QLabel(mainWidget);
            m_previewer->setScaledContents(true);

            m_previewArea = new QScrollArea(mainWidget);
            m_previewArea->setBackgroundRole(QPalette::Dark);
            m_previewArea->setWidget(m_previewer);
            m_previewArea->setMinimumSize(256, 256);

            listLayout->addWidget(m_previewArea);
        }

        mainLayout->addLayout(listLayout);
    }

    setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    setWindowTitle(p_title);
}

QVector<ConfirmItemInfo> DeleteConfirmDialog::getConfirmedItems() const
{
    QVector<ConfirmItemInfo> confirmedItems;

    for (int i = 0; i < m_listWidget->count(); ++i) {
        SelectionItemWidget *widget = getItemWidget(m_listWidget->item(i));
        if (widget->isChecked()) {
            confirmedItems.push_back(m_items[widget->getData().toInt()]);
        }
    }

    return confirmedItems;
}

void DeleteConfirmDialog::updateItemsList()
{
    m_listWidget->clear();

    for (int i = 0; i < m_items.size(); ++i) {
        auto itemWidget = new SelectionItemWidget(m_items[i].m_icon, m_items[i].m_name, this);
        itemWidget->setChecked(true);
        itemWidget->setData(i);
        itemWidget->setToolTip(m_items[i].m_tip);
        connect(itemWidget, &SelectionItemWidget::checkStateChanged,
                this, &DeleteConfirmDialog::updateCountLabel);

        QListWidgetItem *item = new QListWidgetItem(m_listWidget);
        QSize size = itemWidget->sizeHint();
        size.setHeight(size.height() * 2);
        item->setSizeHint(size);

        m_listWidget->setItemWidget(item, itemWidget);
    }

    m_listWidget->setMinimumSize(m_listWidget->sizeHint());
    m_listWidget->setCurrentRow(-1);
    m_listWidget->setCurrentRow(0);
}

bool DeleteConfirmDialog::isNoAskChecked() const
{
    return m_noAskCB->isChecked();
}

void DeleteConfirmDialog::currentFileChanged(int p_row)
{
    if (m_previewer) {
        bool succeed = false;
        if (p_row > -1) {
            SelectionItemWidget *widget = getItemWidget(m_listWidget->item(p_row));
            if (widget) {
                int idx = widget->getData().toInt();
                Q_ASSERT(idx < m_items.size());
                QPixmap image(m_items[idx].m_path);
                if (!image.isNull()) {
                    m_previewer->setPixmap(image);
                    m_previewer->adjustSize();
                    succeed = true;
                }
            }
        }

        m_previewArea->setVisible(succeed);
        if (succeed) {
            resizeToHideScrollBarLater(true, true);
        }
    }
}

SelectionItemWidget *DeleteConfirmDialog::getItemWidget(QListWidgetItem *p_item) const
{
    QWidget *wid = m_listWidget->itemWidget(p_item);
    return static_cast<SelectionItemWidget *>(wid);
}

void DeleteConfirmDialog::updateCountLabel()
{
    int total = m_listWidget->count();
    int checked = 0;

    for (int i = 0; i < total; ++i) {
        SelectionItemWidget *widget = getItemWidget(m_listWidget->item(i));
        if (widget->isChecked()) {
            ++checked;
        }
    }

    m_countLabel->setText(tr("%1/%2 Items").arg(checked).arg(total));
}
