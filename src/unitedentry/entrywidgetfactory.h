#ifndef ENTRYWIDGETFACTORY_H
#define ENTRYWIDGETFACTORY_H

#include <QSharedPointer>

class QTreeWidget;
class QLabel;
class QString;

namespace vnotex
{
    class EntryWidgetFactory
    {
    public:
        EntryWidgetFactory() = delete;

        static QSharedPointer<QTreeWidget> createTreeWidget(int p_columnCount);

        static QSharedPointer<QLabel> createLabel(const QString &p_info);
    };
}

#endif // ENTRYWIDGETFACTORY_H
