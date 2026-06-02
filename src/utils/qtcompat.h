#ifndef VNOTE_QTCOMPAT_H
#define VNOTE_QTCOMPAT_H

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QByteArrayView>
#include <QEnterEvent>
#else
#include <QByteArray>
#include <QEvent>
#endif

namespace vnotex {

// Type alias for the buffer view type. On Qt6 this is a non-owning
// QByteArrayView; on Qt5 we fall back to QByteArray (cheap COW) since
// QByteArrayView does not exist. Both are convertible to QString::fromUtf8.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using QByteArrayViewCompat = QByteArrayView;
#else
using QByteArrayViewCompat = QByteArray;
#endif

// Type alias for the parameter type of QWidget::enterEvent.
// Qt6 introduced QEnterEvent; Qt5 still uses QEvent here.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using EnterEventCompat = QEnterEvent;
#else
using EnterEventCompat = QEvent;
#endif

} // namespace vnotex

#endif // VNOTE_QTCOMPAT_H
