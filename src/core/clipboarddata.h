#ifndef CLIPBOARDDATA_H
#define CLIPBOARDDATA_H

#include <QVector>
#include <QJsonObject>
#include <QSharedPointer>

#include "global.h"

namespace vnotex
{
    class ClipboardDataItem
    {
    public:
        virtual ~ClipboardDataItem()
        {
        }

        virtual QJsonObject toJson() const = 0;

        virtual void fromJson(const QJsonObject &p_jobj) = 0;
    };

    class NodeClipboardDataItem : public ClipboardDataItem
    {
    public:
        NodeClipboardDataItem();

        NodeClipboardDataItem(ID p_notebookId, const QString &p_nodePath);

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        void fromJson(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

        ID m_notebookId;
        QString m_nodeRelativePath;

    private:
        static const QString c_notebookId;
        static const QString c_nodePath;
    };

    class ClipboardData
    {
    public:
        enum Action { CopyNode, MoveNode, Invalid };

        ClipboardData();

        ClipboardData(ID p_instanceId, Action p_action);

        ID getInstanceId() const;

        ClipboardData::Action getAction() const;

        const QVector<QSharedPointer<ClipboardDataItem>> &getData() const;

        void addItem(const QSharedPointer<ClipboardDataItem> &p_item);

        QString toJsonText() const;

        static QSharedPointer<ClipboardData> fromJsonText(const QString &p_json);

    private:
        void fromJson(const QJsonObject &p_jobj);

        QJsonObject toJson() const;

        ClipboardData::Action intToAction(int p_act) const;

        void clear();

        static QSharedPointer<ClipboardDataItem> createClipboardDataItem(Action p_act);

        ID m_instanceId = 0;
        Action m_action = Action::Invalid;
        QVector<QSharedPointer<ClipboardDataItem>> m_data;

        static const QString c_instanceId;
        static const QString c_action;
        static const QString c_data;
    };
} // ns vnotex

#endif // CLIPBOARDDATA_H
