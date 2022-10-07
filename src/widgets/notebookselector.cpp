#include "notebookselector.h"

#include <QKeyEvent>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QLabel>
#include <QListView>
#include <QScrollBar>

#include <notebook/notebook.h>
#include <core/notebookmgr.h>
#include <core/vnotex.h>
#include <utils/iconutils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

NotebookSelector::NotebookSelector(QWidget *p_parent)
    : ComboBox(p_parent),
      NavigationMode(NavigationMode::Type::StagedDoubleKeys, this)
{
    auto itemDelegate = new QStyledItemDelegate(this);
    setItemDelegate(itemDelegate);

    view()->installEventFilter(this);

    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
}

void NotebookSelector::loadNotebooks()
{
    clear();

    auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    auto notebooks = notebookMgr.getNotebooks();
    sortNotebooks(notebooks);

    for (auto &nb : notebooks) {
        addNotebookItem(nb);
    }

    updateGeometry();

    m_notebooksInitialized = true;
}

void NotebookSelector::sortNotebooks(QVector<QSharedPointer<Notebook>> &p_notebooks) const
{
    bool reversed = false;
    switch (m_viewOrder) {
    case ViewOrder::OrderedByNameReversed:
        reversed = true;
        Q_FALLTHROUGH();
    case ViewOrder::OrderedByName:
        std::sort(p_notebooks.begin(), p_notebooks.end(), [reversed](const QSharedPointer<Notebook> &p_a, const QSharedPointer<Notebook> &p_b) {
            if (reversed) {
                return p_b->getName().toLower() < p_a->getName().toLower();
            } else {
                return p_a->getName().toLower() < p_b->getName().toLower();
            }
        });
        break;

    case ViewOrder::OrderedByCreatedTimeReversed:
        reversed = true;
        Q_FALLTHROUGH();
    case ViewOrder::OrderedByCreatedTime:
        std::sort(p_notebooks.begin(), p_notebooks.end(), [reversed](const QSharedPointer<Notebook> &p_a, const QSharedPointer<Notebook> &p_b) {
            if (reversed) {
                return p_b->getCreatedTimeUtc() < p_a->getCreatedTimeUtc();
            } else {
                return p_a->getCreatedTimeUtc() < p_b->getCreatedTimeUtc();
            }
        });
        break;

    default:
        break;
    }
}

void NotebookSelector::reloadNotebook(const Notebook *p_notebook)
{
    Q_ASSERT(p_notebook);
    int idx = findNotebook(p_notebook->getId());
    Q_ASSERT(idx != -1);

    setItemIcon(idx, generateItemIcon(p_notebook));
    setItemText(idx, p_notebook->getName());
    setItemToolTip(idx, generateItemToolTip(p_notebook));

    int curIdx = currentIndex();
    if (curIdx == idx) {
        setToolTip(getItemToolTip(idx));
    }
}

void NotebookSelector::addNotebookItem(const QSharedPointer<Notebook> &p_notebook)
{
    int idx = count();
    addItem(generateItemIcon(p_notebook.data()), p_notebook->getName(), p_notebook->getId());
    setItemToolTip(idx, generateItemToolTip(p_notebook.data()));
}

void NotebookSelector::fetchIconColor(const QString &p_name, QString &p_fg, QString &p_bg)
{
    static QVector<QString> backgroundColors = {
        "#80558c",
        "#df7861",
        "#f65a83",
        "#3b9ae1",
        "#277bc0",
        "#42855b",
        "#a62349",
        "#a66cff",
        "#9c9efe",
        "#54bab9",
        "#79b4b7",
        "#57cc99",
        "#916bbf",
        "#5c7aea",
        "#6867ac",
    };

    int hashVal = 0;
    for (int i = 0; i < p_name.size(); ++i) {
        hashVal += p_name[i].unicode();
    }

    p_fg = "#ffffff";
    p_bg = backgroundColors[hashVal % backgroundColors.size()];
}

QIcon NotebookSelector::generateItemIcon(const Notebook *p_notebook)
{
    if (!p_notebook->getIcon().isNull()) {
        return p_notebook->getIcon();
    }

    QString fg, bg;
    fetchIconColor(p_notebook->getName(), fg, bg);
    return IconUtils::drawTextRectIcon(p_notebook->getName().at(0).toUpper(),
                                       fg,
                                       bg,
                                       "",
                                       50,
                                       58);
}

QString NotebookSelector::generateItemToolTip(const Notebook *p_notebook)
{
    return tr("Notebook: %1\nRoot folder: %2\nDescription: %3")
             .arg(p_notebook->getName(),
                  p_notebook->getRootFolderAbsolutePath(),
                  p_notebook->getDescription());
}

QString NotebookSelector::getItemToolTip(int p_idx) const
{
    return itemData(p_idx, Qt::ToolTipRole).toString();
}

void NotebookSelector::setItemToolTip(int p_idx, const QString &p_tooltip)
{
    setItemData(p_idx, p_tooltip, Qt::ToolTipRole);
}

void NotebookSelector::setCurrentNotebook(ID p_id)
{
    int idx = findNotebook(p_id);
    setCurrentIndex(idx);
    setToolTip(getItemToolTip(idx));
}

int NotebookSelector::findNotebook(ID p_id) const
{
    return findData(p_id);
}

QVector<void *> NotebookSelector::getVisibleNavigationItems()
{
    QVector<void *> items;
    auto listView = dynamic_cast<QListView *>(view());
    if (listView) {
        m_navigationIndexes = WidgetUtils::getVisibleIndexes(listView);
        for (auto &index : m_navigationIndexes) {
            items.push_back(&index);
        }
    }
    return items;
}

void NotebookSelector::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label)
{
    if (p_idx == -1) {
        // Major.
        p_label->move(rect().topRight() - QPoint(p_label->width() + 2, 2));
    } else {
        // Second.
        // Reparent it to the list view.
        auto listView = view();
        p_label->setParent(listView);

        auto index = *static_cast<QModelIndex *>(p_item);

        int extraWidth = p_label->width() + 2;
        auto vbar = listView->verticalScrollBar();
        if (vbar && vbar->minimum() != vbar->maximum()) {
            extraWidth += vbar->width();
        }

        const auto rt = listView->visualRect(index);
        const int x = rt.x() + view()->width() - extraWidth;
        const int y = rt.y();
        p_label->move(x, y);
    }
}

void NotebookSelector::handleTargetHit(void *p_item)
{
    if (!p_item) {
        setFocus();
        showPopup();
    } else {
        hidePopup();
        auto index = *static_cast<QModelIndex *>(p_item);
        setCurrentIndex(index.row());
        emit activated(index.row());
    }
}

bool NotebookSelector::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (p_event->type() == QEvent::KeyPress && p_obj == view()) {
        if (WidgetUtils::processKeyEventLikeVi(view(), static_cast<QKeyEvent *>(p_event))) {
            return true;
        }
    }
    return ComboBox::eventFilter(p_obj, p_event);
}

void NotebookSelector::clearNavigation()
{
    m_navigationIndexes.clear();

    NavigationMode::clearNavigation();
}

void NotebookSelector::mousePressEvent(QMouseEvent *p_event)
{
    // Only when notebooks are loaded and there is no notebook, we could prompt for new notebook.
    if (m_notebooksInitialized && count() == 0) {
        emit newNotebookRequested();
        return;
    }

    ComboBox::mousePressEvent(p_event);
}

void NotebookSelector::setViewOrder(int p_order)
{
    if (m_viewOrder == p_order) {
        return;
    }

    if (p_order >= 0 && p_order < ViewOrder::ViewOrderMax) {
        m_viewOrder = static_cast<ViewOrder>(p_order);
        loadNotebooks();
    }
}
