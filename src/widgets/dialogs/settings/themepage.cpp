#include "themepage.h"

#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QGroupBox>
#include <QDebug>
#include <QUrl>
#include <QScrollArea>

#include <widgets/listwidget.h>
#include <QPushButton>
#include <QListWidgetItem>
#include <widgets/widgetsfactory.h>
#include <core/thememgr.h>
#include <core/vnotex.h>
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <utils/widgetutils.h>

using namespace vnotex;

ThemePage::ThemePage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void ThemePage::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    // Theme.
    {
        auto layout = new QGridLayout();
        mainLayout->addLayout(layout);

        m_themeListWidget = new ListWidget(this);
        layout->addWidget(m_themeListWidget, 0, 0, 3, 2);
        connect(m_themeListWidget, &QListWidget::currentItemChanged,
                this, [this](QListWidgetItem *p_current, QListWidgetItem *p_previous) {
                    Q_UNUSED(p_previous);
                    loadThemePreview(p_current ? p_current->data(Qt::UserRole).toString() : QString());
                    pageIsChanged();
                });

        auto refreshBtn = new QPushButton(tr("Refresh"), this);
        layout->addWidget(refreshBtn, 3, 0, 1, 1);
        connect(refreshBtn, &QPushButton::clicked,
                this, [this]() {
                    VNoteX::getInst().getThemeMgr().refresh();
                    loadThemes();
                });

        auto addBtn = new QPushButton(tr("Add/Delete"), this);
        layout->addWidget(addBtn, 3, 1, 1, 1);
        // TODO: open an editor to edit the theme list.
        connect(addBtn, &QPushButton::clicked,
                this, []() {
                WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(ConfigMgr::getInst().getUserThemeFolder()));
                });

        auto updateBtn = new QPushButton(tr("Update"), this);
        layout->addWidget(updateBtn, 4, 0, 1, 1);

        auto openLocationBtn = new QPushButton(tr("Open Location"), this);
        layout->addWidget(openLocationBtn, 4, 1, 1, 1);
        connect(openLocationBtn, &QPushButton::clicked,
                this, [this]() {
                    auto theme = VNoteX::getInst().getThemeMgr().findTheme(currentTheme());
                    if (theme) {
                        WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(theme->m_folderPath));
                    }
                });

        m_noPreviewText = tr("No Preview Available");
        m_previewLabel = new QLabel(m_noPreviewText, this);
        m_previewLabel->setScaledContents(true);
        m_previewLabel->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        auto scrollArea = new QScrollArea(this);
        scrollArea->setBackgroundRole(QPalette::Dark);
        scrollArea->setWidget(m_previewLabel);
        scrollArea->setMinimumSize(256, 256);
        layout->addWidget(scrollArea, 0, 2, 5, 1);
    }

    // Override.
    {
        auto box = new QGroupBox(tr("Style Override"), this);
        mainLayout->addWidget(box);
    }
}

void ThemePage::loadInternal()
{
    loadThemes();
}

void ThemePage::saveInternal()
{
    auto theme = currentTheme();
    if (!theme.isEmpty()) {
        ConfigMgr::getInst().getCoreConfig().setTheme(theme);
    }
}

QString ThemePage::title() const
{
    return tr("Theme");
}

void ThemePage::loadThemes()
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto &themes = themeMgr.getAllThemes();

    m_themeListWidget->clear();
    for (const auto &info : themes) {
        auto item = new QListWidgetItem(info.m_displayName, m_themeListWidget);
        item->setData(Qt::UserRole, info.m_name);
        item->setToolTip(info.m_folderPath);
    }

    // Set current theme.
    bool found = false;
    const auto curThemeName = themeMgr.getCurrentTheme().name();
    for (int i = 0; i < m_themeListWidget->count(); ++i) {
        if (m_themeListWidget->item(i)->data(Qt::UserRole).toString() == curThemeName) {
            m_themeListWidget->setCurrentRow(i);
            found = true;
            break;
        }
    }

    if (!found && m_themeListWidget->count() > 0) {
        m_themeListWidget->setCurrentRow(0);
    }
}

void ThemePage::loadThemePreview(const QString &p_name)
{
    if (p_name.isEmpty()) {
        m_previewLabel->setText(m_noPreviewText);
    }

    auto pixmap = VNoteX::getInst().getThemeMgr().getThemePreview(p_name);
    if (pixmap.isNull()) {
        m_previewLabel->setText(m_noPreviewText);
    } else {
        const int pwidth = 512;
        m_previewLabel->setPixmap(pixmap.scaledToWidth(pwidth, Qt::SmoothTransformation));
    }
    m_previewLabel->adjustSize();
}

QString ThemePage::currentTheme() const
{
    auto item = m_themeListWidget->currentItem();
    if (item) {
        return item->data(Qt::UserRole).toString();
    }
    return QString();
}
