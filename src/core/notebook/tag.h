#ifndef TAG_H
#define TAG_H

#include <QString>
#include <QVector>
#include <QSharedPointer>
#include <QEnableSharedFromThis>

namespace vnotex
{
    class Tag : public QEnableSharedFromThis<Tag>
    {
    public:
        Tag(const QString &p_name);

        const QVector<QSharedPointer<Tag>> &getChildren() const;

        const QString &name() const;

        Tag *getParent() const;

        void addChild(const QSharedPointer<Tag> &p_tag);

        QString fetchPath() const;

        static bool isValidName(const QString &p_name);

    private:
        Tag *m_parent = nullptr;

        QString m_name;

        QVector<QSharedPointer<Tag>> m_children;
    };
}

#endif // TAG_H
