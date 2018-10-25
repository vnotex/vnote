#include "vcombobox.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>

VComboBox::VComboBox(QWidget *p_parent)
    : QComboBox(p_parent)
{
}

void VComboBox::showPopup()
{
    QComboBox::showPopup();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    auto *vw = view();
    if (count() > 0) {
        int cnt = qMin(count(), maxVisibleItems());
        int height = 20 + cnt * vw->visualRect(vw->model()->index(0, 0)).height();
        if (height > vw->height()) {
            vw->setMinimumHeight(height);
        }
    }
#endif
}
