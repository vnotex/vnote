#include "listwidget.h"

#include <QKeyEvent>

#include <core/vnotex.h>
#include <core/thememgr.h>
#include <utils/widgetutils.h>
#include "styleditemdelegate.h"

using namespace vnotex;

QBrush ListWidget::s_separatorForeground;

QBrush ListWidget::s_separatorBackground;

ListWidget::ListWidget(QWidget *p_parent)
    : QListWidget(p_parent)
{
    initialize();
}

ListWidget::ListWidget(bool p_enhancedStyle, QWidget *p_parent)
    : QListWidget(p_parent)
{
    initialize();

    if (p_enhancedStyle) {
        auto delegate = new StyledItemDelegate(QSharedPointer<StyledItemDelegateListWidget>::create(this),
                                               StyledItemDelegate::None,
                                               this);
        setItemDelegate(delegate);
    }
}

void ListWidget::initialize()
{
    static bool initialized = false;
    if (!initialized) {
        initialized = true;

        const auto &themeMgr = VNoteX::getInst().getThemeMgr();
        s_separatorForeground = QColor(themeMgr.paletteColor(QStringLiteral("widgets#styleditemdelegate#separator#fg")));
        s_separatorBackground = QColor(themeMgr.paletteColor(QStringLiteral("widgets#styleditemdelegate#separator#bg")));
    }
}

void ListWidget::keyPressEvent(QKeyEvent *p_event)
{
    if (WidgetUtils::processKeyEventLikeVi(this, p_event)) {
        return;
    }

    // On Mac OS X, it is `Command+O` to activate an item, instead of Return.
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (p_event->key() == Qt::Key_Return) {
        if (auto item = currentItem()) {
            emit itemActivated(item);
        }
        return;
    }
#endif

    QListWidget::keyPressEvent(p_event);
}

QVector<QListWidgetItem *> ListWidget::getVisibleItems(const QListWidget *p_widget)
{
    QVector<QListWidgetItem *> items;

    auto firstItem = p_widget->itemAt(0, 0);
    if (!firstItem) {
        return items;
    }

    auto lastItem = p_widget->itemAt(p_widget->viewport()->rect().bottomLeft());

    int firstRow = p_widget->row(firstItem);
    int lastRow = lastItem ? p_widget->row(lastItem) : (p_widget->count() - 1);
    for (int i = firstRow; i <= lastRow; ++i) {
        auto item = p_widget->item(i);
        if (!item->isHidden() && item->flags() != Qt::NoItemFlags) {
            items.append(item);
        }
    }

    return items;
}

QListWidgetItem *ListWidget::createSeparatorItem(const QString &p_text)
{
    QListWidgetItem *item = new QListWidgetItem(p_text, nullptr, ItemTypeSeparator);
    item->setData(Qt::ForegroundRole, s_separatorForeground);
    item->setData(Qt::BackgroundRole, s_separatorBackground);
    item->setFlags(Qt::NoItemFlags);
    return item;
}

bool ListWidget::isSeparatorItem(const QListWidgetItem *p_item)
{
    return p_item->type() == ItemTypeSeparator;
}
