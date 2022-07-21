#include "locationlist.h"

#include <QVBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QHeaderView>

#include "treewidget.h"
#include "widgetsfactory.h"
#include "titlebar.h"
#include "styleditemdelegate.h"
#include "navigationmodemgr.h"

#include <core/vnotex.h>
#include <core/thememgr.h>
#include <utils/iconutils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

QIcon LocationList::s_bufferIcon;

QIcon LocationList::s_fileIcon;

QIcon LocationList::s_folderIcon;

QIcon LocationList::s_notebookIcon;

LocationList::LocationList(QWidget *p_parent)
    : QFrame(p_parent)
{
    setupUI();
}

void LocationList::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);

    {
        setupTitleBar(QString(), this);
        mainLayout->addWidget(m_titleBar);
    }

    m_tree = new TreeWidget(TreeWidget::Flag::EnhancedStyle, this);
    // When updated, pay attention to the Columns enum.
    m_tree->setHeaderLabels(QStringList() << tr("Path") << tr("Line") << tr("Text"));
    m_tree->header()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->header()->setStretchLastSection(true);
    connect(m_tree, &QTreeWidget::itemActivated,
            this, [this](QTreeWidgetItem *p_item, int p_col) {
                Q_UNUSED(p_col);
                if (!m_callback) {
                    return;
                }
                m_callback(getItemLocation(p_item));
            });
    mainLayout->addWidget(m_tree);

    m_navigationWrapper.reset(new NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>(m_tree));
    NavigationModeMgr::getInst().registerNavigationTarget(m_navigationWrapper.data());

    setFocusProxy(m_tree);
}

const QIcon &LocationList::getItemIcon(LocationType p_type)
{
    if (s_bufferIcon.isNull()) {
        // Init.
        const QString nodeIconFgName = "widgets#locationlist#node_icon#fg";
        const auto &themeMgr = VNoteX::getInst().getThemeMgr();
        const auto fg = themeMgr.paletteColor(nodeIconFgName);

        s_bufferIcon = IconUtils::fetchIcon(themeMgr.getIconFile("buffer.svg"), fg);
        s_fileIcon = IconUtils::fetchIcon(themeMgr.getIconFile("file_node.svg"), fg);
        s_folderIcon = IconUtils::fetchIcon(themeMgr.getIconFile("folder_node.svg"), fg);
        s_notebookIcon = IconUtils::fetchIcon(themeMgr.getIconFile("notebook_default.svg"), fg);
    }

    switch (p_type) {
    case LocationType::Buffer:
        return s_bufferIcon;

    case LocationType::File:
        return s_fileIcon;

    case LocationType::Folder:
        return s_folderIcon;

    case LocationType::Notebook:
        Q_FALLTHROUGH();
    default:
        return s_notebookIcon;
    }
}

void LocationList::setupTitleBar(const QString &p_title, QWidget *p_parent)
{
    m_titleBar = new TitleBar(p_title, true, TitleBar::Action::None, p_parent);
    m_titleBar->setActionButtonsAlwaysShown(true);

    {
        auto clearBtn = m_titleBar->addActionButton(QStringLiteral("clear.svg"), tr("Clear"));
        connect(clearBtn, &QToolButton::triggered,
                this, &LocationList::clear);
    }
}

void LocationList::clear()
{
    m_tree->clear();

    m_callback = LocationCallback();

    updateItemsCountLabel();
}

void LocationList::setItemLocationLineAndText(QTreeWidgetItem *p_item, const ComplexLocation::Line &p_line)
{
    p_item->setData(Columns::LineColumn, Qt::UserRole, p_line.m_lineNumber);
    if (p_line.m_lineNumber != -1) {
        p_item->setText(Columns::LineColumn, QString::number(p_line.m_lineNumber + 1));
    }

    // Truncate the text.
    if (p_line.m_text.size() > 500) {
        p_item->setText(Columns::TextColumn, p_line.m_text.left(500));
    } else {
        p_item->setText(Columns::TextColumn, p_line.m_text);
    }

    if (!p_line.m_segments.isEmpty()) {
        p_item->setData(Columns::TextColumn, HighlightsRole, QVariant::fromValue(p_line.m_segments));
    }
}

void LocationList::addLocation(const ComplexLocation &p_location)
{
    auto item = new QTreeWidgetItem(m_tree);
    item->setText(Columns::PathColumn, p_location.m_displayPath);
    item->setIcon(Columns::PathColumn, getItemIcon(p_location.m_type));
    item->setData(Columns::PathColumn, Qt::UserRole, p_location.m_path);
    item->setToolTip(Columns::PathColumn, p_location.m_path);

    if (p_location.m_lines.size() == 1) {
        setItemLocationLineAndText(item, p_location.m_lines[0]);
    } else if (p_location.m_lines.size() > 1) {
        // Add sub items.
        for (const auto &line : p_location.m_lines) {
            auto subItem = new QTreeWidgetItem(item);
            setItemLocationLineAndText(subItem, line);
        }

        item->setExpanded(true);
    }

    if (m_tree->topLevelItemCount() == 1) {
        m_tree->setCurrentItem(item);
    }

    updateItemsCountLabel();
}

void LocationList::startSession(const LocationCallback &p_callback)
{
    m_callback = p_callback;
}

Location LocationList::getItemLocation(const QTreeWidgetItem *p_item) const
{
    Location loc;

    if (!p_item) {
        return loc;
    }

    auto paItem = p_item->parent() ? p_item->parent() : p_item;
    loc.m_path = paItem->data(Columns::PathColumn, Qt::UserRole).toString();
    loc.m_displayPath = paItem->text(Columns::PathColumn);

    auto lineNumberData = p_item->data(Columns::LineColumn, Qt::UserRole);
    if (lineNumberData.isValid()) {
        loc.m_lineNumber = lineNumberData.toInt();
    }
    return loc;
}

void LocationList::updateItemsCountLabel()
{
    const auto cnt = m_tree->topLevelItemCount();
    if (cnt == 0) {
        m_titleBar->setInfoLabel("");
    } else {
        m_titleBar->setInfoLabel(tr("%n Item(s)", "", cnt));
    }
}

QByteArray LocationList::saveState() const
{
    return m_tree->header()->saveState();
}

void LocationList::restoreState(const QByteArray &p_data)
{
    m_tree->header()->restoreState(p_data);
}
