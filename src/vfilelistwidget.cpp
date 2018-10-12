#include "vfilelistwidget.h"

#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMimeData>

#include "vconstants.h"

VFileListWidget::VFileListWidget(QWidget *p_parent)
    : VListWidget(p_parent)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setMouseTracking(true);
}

QStringList VFileListWidget::mimeTypes() const
{
    return QStringList(ClipboardConfig::c_format);
}

QMimeData *VFileListWidget::mimeData(const QList<QListWidgetItem *> p_items) const
{
    const QString format(ClipboardConfig::c_format);
    QStringList types = mimeTypes();
    if (!types.contains(format) || p_items.isEmpty()) {
        return NULL;
    }

    if (m_mimeDataGetter) {
        QMimeData *data = new QMimeData();
        data->setData(format, m_mimeDataGetter(format, p_items));
        return data;
    }

    return NULL;
}

void VFileListWidget::setMimeDataGetter(const MimeDataGetterFunc &p_getter)
{
    m_mimeDataGetter = p_getter;
}
