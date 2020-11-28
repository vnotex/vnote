#include "notebookselector.h"

#include <QKeyEvent>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QLabel>
#include <QListView>
#include <QScrollBar>

#include "vnotex.h"
#include "notebook/notebook.h"
#include <utils/iconutils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

NotebookSelector::NotebookSelector(QWidget *p_parent)
    : QComboBox(p_parent),
      NavigationMode(NavigationMode::Type::StagedDoubleKeys, this)
{
    auto itemDelegate = new QStyledItemDelegate(this);
    setItemDelegate(itemDelegate);

    view()->installEventFilter(this);

    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
}

void NotebookSelector::setNotebooks(const QVector<QSharedPointer<Notebook>> &p_notebooks)
{
    clear();

    for (auto &nb : p_notebooks) {
        addNotebookItem(nb);
    }

    updateGeometry();
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

QIcon NotebookSelector::generateItemIcon(const Notebook *p_notebook)
{
    if (!p_notebook->getIcon().isNull()) {
        return p_notebook->getIcon();
    }

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    QString iconFile;
    const auto &type = p_notebook->getType();
    if (type == "native.vnotex") {
        iconFile = themeMgr.getIconFile("native_notebook_default.svg");
    } else {
        iconFile = themeMgr.getIconFile("notebook_default.svg");
    }

    return IconUtils::fetchIcon(iconFile);
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
    return QComboBox::eventFilter(p_obj, p_event);
}

void NotebookSelector::clearNavigation()
{
    m_navigationIndexes.clear();

    NavigationMode::clearNavigation();
}
