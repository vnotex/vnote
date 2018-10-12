#ifndef VFILELISTWIDGET_H
#define VFILELISTWIDGET_H

#include "vlistwidget.h"

#include <functional>
#include <QList>

typedef std::function<QByteArray(const QString &, const QList<QListWidgetItem *> &)> MimeDataGetterFunc;

class VFileListWidget : public VListWidget
{
    Q_OBJECT
public:
    explicit VFileListWidget(QWidget *p_parent = nullptr);

    void setMimeDataGetter(const MimeDataGetterFunc &p_getter);

protected:
    QStringList mimeTypes() const Q_DECL_OVERRIDE;

    QMimeData *mimeData(const QList<QListWidgetItem *> p_items) const Q_DECL_OVERRIDE;

private:
    MimeDataGetterFunc m_mimeDataGetter;
};

#endif // VFILELISTWIDGET_H
