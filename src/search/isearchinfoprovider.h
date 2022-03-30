#ifndef ISEARCHINFOPROVIDER_H
#define ISEARCHINFOPROVIDER_H

#include <QList>
#include <QVector>

namespace vnotex
{
    class Node;
    class Notebook;
    class Buffer;

    class ISearchInfoProvider
    {
    public:
        ISearchInfoProvider() = default;

        virtual ~ISearchInfoProvider() = default;

        virtual QList<Buffer *> getBuffers() const = 0;

        virtual Node *getCurrentFolder() const = 0;

        virtual Notebook *getCurrentNotebook() const = 0;

        virtual QVector<Notebook *> getNotebooks() const = 0;
    };
}

#endif // ISEARCHINFOPROVIDER_H
