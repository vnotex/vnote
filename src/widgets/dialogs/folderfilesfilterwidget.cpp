#include "folderfilesfilterwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QDir>
#include <QFileDialog>
#include <QListWidget>
#include <QSet>
#include <QDebug>

#include "../widgetsfactory.h"
#include "../lineedit.h"
#include <utils/pathutils.h>
#include <utils/widgetutils.h>
#include "selectionitemwidget.h"

using namespace vnotex;

FolderFilesFilterWidget::FolderFilesFilterWidget(QWidget *p_parent)
    : QWidget(p_parent)
{
    m_scanTimer = new QTimer(this);
    m_scanTimer->setSingleShot(true);
    m_scanTimer->setInterval(2000);
    connect(m_scanTimer, &QTimer::timeout,
            this, &FolderFilesFilterWidget::scanSuffixes);

    setupUI();
}

void FolderFilesFilterWidget::setupUI()
{
    auto mainLayout = WidgetUtils::createFormLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    {
        auto pathLayout = new QHBoxLayout();
        pathLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addRow(tr("Folder:"), pathLayout);

        m_folderPathEdit = WidgetsFactory::createLineEdit(this);
        pathLayout->addWidget(m_folderPathEdit);
        connect(m_folderPathEdit, &QLineEdit::textChanged,
                this, [this]() {
                    m_ready = false;
                    m_scanTimer->start();
                });

        auto browseBtn = new QPushButton(tr("Browse"), this);
        pathLayout->addWidget(browseBtn);
        connect(browseBtn, &QPushButton::clicked,
                this, [this]() {
                    static QString lastBrowsePath = QDir::homePath();
                    auto folderPath = QFileDialog::getExistingDirectory(this,
                        tr("Select Notebook Root Folder"),
                        lastBrowsePath,
                        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
                    if (!folderPath.isEmpty()) {
                        lastBrowsePath = PathUtils::parentDirPath(folderPath);
                        m_folderPathEdit->setText(folderPath);
                    }
                });
    }

    {
        auto layout = new QHBoxLayout();
        mainLayout->addRow(tr("Select files:"), layout);
        layout->setContentsMargins(0, 0, 0, 0);

        m_suffixList = new QListWidget(this);
        layout->addWidget(m_suffixList);

        auto btnLayout = new QVBoxLayout();
        layout->addLayout(btnLayout);
        btnLayout->setContentsMargins(0, 0, 0, 0);

        auto selectAllBtn = new QPushButton(tr("Select All"), this);
        btnLayout->addWidget(selectAllBtn);
        connect(selectAllBtn, &QPushButton::clicked,
                this, [this]() {
                    for (int i = 0; i < m_suffixList->count(); ++i) {
                        SelectionItemWidget *widget = getItemWidget(m_suffixList->item(i));
                        widget->setChecked(true);
                    }
                });

        auto clearBtn = new QPushButton(tr("Clear"), this);
        btnLayout->addWidget(clearBtn);
        connect(clearBtn, &QPushButton::clicked,
                this, [this]() {
                    for (int i = 0; i < m_suffixList->count(); ++i) {
                        SelectionItemWidget *widget = getItemWidget(m_suffixList->item(i));
                        widget->setChecked(false);
                    }
                });

        btnLayout->addStretch();
    }
}

QString FolderFilesFilterWidget::getFolderPath() const
{
    Q_ASSERT(m_ready);
    return m_folderPathEdit->text();
}

QStringList FolderFilesFilterWidget::getSuffixes() const
{
    Q_ASSERT(m_ready);
    QStringList suffixes;
    for (int i = 0; i < m_suffixList->count(); ++i) {
        auto widget = getItemWidget(m_suffixList->item(i));
        if (widget->isChecked()) {
            suffixes << widget->getData().toString();
        }
    }

    return suffixes;
}

static void scanDir(const QString &p_path, QSet<QString> &p_suffixes)
{
    QDir dir(p_path);
    auto children = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const auto &child : children) {
        if (child.isDir()) {
            scanDir(child.filePath(), p_suffixes);
        } else {
            if (!child.suffix().isEmpty()) {
                p_suffixes.insert(child.suffix());
            }
        }
    }
}

static bool shouldSuffixBeChecked(const QString &p_suffix)
{
    QStringList suffixes = {QStringLiteral("md"),
                            QStringLiteral("markdown"),
                            QStringLiteral("cpp"),
                            QStringLiteral("py"),
                            QStringLiteral("js"),
                            QStringLiteral("css"),
                            QStringLiteral("html"),
                            QStringLiteral("txt")};
    auto suf = p_suffix.toLower();
    return suffixes.contains(suf);
}

void FolderFilesFilterWidget::scanSuffixes()
{
    m_suffixList->clear();

    auto folderPath = m_folderPathEdit->text();
    bool validFolderPath = QFileInfo::exists(folderPath) && PathUtils::isLegalPath(folderPath);
    if (validFolderPath) {
        WidgetUtils::setPropertyDynamically(m_folderPathEdit, "State");

        QSet<QString> suffixes;
        scanDir(folderPath, suffixes);

        for (const auto &suffix : suffixes) {
            auto itemWidget = new SelectionItemWidget(suffix, this);
            itemWidget->setChecked(shouldSuffixBeChecked(suffix));
            itemWidget->setData(suffix);

            QListWidgetItem *item = new QListWidgetItem(m_suffixList);
            QSize size = itemWidget->sizeHint();
            size.setHeight(size.height() * 2);
            item->setSizeHint(size);

            m_suffixList->setItemWidget(item, itemWidget);
        }
    } else {
        WidgetUtils::setPropertyDynamically(m_folderPathEdit, "State", "error");
        qWarning() << "invalid folder path" << folderPath;
    }

    WidgetUtils::updateSize(m_suffixList);

    m_ready = true;
    emit filesChanged();
}

SelectionItemWidget *FolderFilesFilterWidget::getItemWidget(QListWidgetItem *p_item) const
{
    QWidget *wid = m_suffixList->itemWidget(p_item);
    return static_cast<SelectionItemWidget *>(wid);
}

bool FolderFilesFilterWidget::isReady() const
{
    return m_ready;
}

QLineEdit *FolderFilesFilterWidget::getFolderPathEdit() const
{
    return m_folderPathEdit;
}
