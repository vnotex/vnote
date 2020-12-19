#include "clipboarddata.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "exception.h"

using namespace vnotex;

const QString NodeClipboardDataItem::c_notebookId = "notebook_id";

const QString NodeClipboardDataItem::c_nodePath = "node_path";

NodeClipboardDataItem::NodeClipboardDataItem()
{
}

NodeClipboardDataItem::NodeClipboardDataItem(ID p_notebookId, const QString &p_nodePath)
    : m_notebookId(p_notebookId),
      m_nodeRelativePath(p_nodePath)
{
}

QJsonObject NodeClipboardDataItem::toJson() const
{
    QJsonObject jobj;
    jobj[c_notebookId] = QString::number(m_notebookId);
    jobj[c_nodePath] = m_nodeRelativePath;
    return jobj;
}

void NodeClipboardDataItem::fromJson(const QJsonObject &p_jobj)
{
    Q_ASSERT(p_jobj.contains(c_notebookId) && p_jobj.contains(c_nodePath));
    auto idRet = stringToID(p_jobj[c_notebookId].toString());
    Q_ASSERT(idRet.first);
    m_notebookId = idRet.second;
    m_nodeRelativePath = p_jobj[c_nodePath].toString();
}


const QString ClipboardData::c_instanceId = "instance_id";

const QString ClipboardData::c_action = "action";

const QString ClipboardData::c_data = "data";

ClipboardData::ClipboardData()
{
}

ClipboardData::ClipboardData(ID p_instanceId, Action p_action)
    : m_instanceId(p_instanceId),
      m_action(p_action)
{
}

void ClipboardData::fromJson(const QJsonObject &p_jobj)
{
    clear();

    if (!p_jobj.contains(c_instanceId)
        || !p_jobj.contains(c_action)
        || !p_jobj.contains(c_data)) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("fail to parse ClipboardData from json (%1)").arg(p_jobj.keys().join(',')));
        return;
    }

    auto idRet = stringToID(p_jobj[c_instanceId].toString());
    if (!idRet.first) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("fail to parse ClipboardData from json (%1)").arg(p_jobj.keys().join(',')));
        return;
    }
    m_instanceId = idRet.second;

    int act = p_jobj[c_action].toInt(Action::Invalid);
    m_action = intToAction(act);
    if (m_action == Action::Invalid) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("fail to parse ClipboardData from json (%1)").arg(p_jobj.keys().join(',')));
        return;
    }

    const auto itemArr = p_jobj[c_data].toArray();
    for (int i = 0; i < itemArr.size(); ++i) {
        auto dataItem = createClipboardDataItem(m_action);
        dataItem->fromJson(itemArr[i].toObject());
        m_data.push_back(dataItem);
    }
}

QJsonObject ClipboardData::toJson() const
{
    QJsonObject jobj;
    jobj[c_instanceId] = QString::number(m_instanceId);
    jobj[c_action] = static_cast<int>(m_action);

    QJsonArray data;
    for (const auto& item : m_data) {
        data.append(item->toJson());
    }
    jobj[c_data] = data;

    return jobj;
}

ClipboardData::Action ClipboardData::intToAction(int p_act) const
{
    Action act = Action::Invalid;
    if (p_act >= Action::CopyNode && p_act < Action::Invalid) {
        act = static_cast<Action>(p_act);
    }

    return act;
}

void ClipboardData::clear()
{
    m_instanceId = 0;
    m_action = Action::Invalid;
    m_data.clear();
}

QSharedPointer<ClipboardDataItem> ClipboardData::createClipboardDataItem(Action p_act)
{
    switch (p_act) {
    case Action::CopyNode:
    case Action::MoveNode:
        return QSharedPointer<NodeClipboardDataItem>::create();

    case Action::Invalid:
        Q_ASSERT(false);
        return nullptr;
    }

    return nullptr;
}

void ClipboardData::addItem(const QSharedPointer<ClipboardDataItem> &p_item)
{
    Q_ASSERT(p_item);
    m_data.push_back(p_item);
}

QString ClipboardData::toJsonText() const
{
    auto data = QJsonDocument(toJson()).toJson();
    return QString::fromUtf8(data);
}

QSharedPointer<ClipboardData> ClipboardData::fromJsonText(const QString &p_json)
{
    if (p_json.isEmpty()) {
        return nullptr;
    }

    auto data = QSharedPointer<ClipboardData>::create();
    auto jobj = QJsonDocument::fromJson(p_json.toUtf8()).object();
    if (jobj.isEmpty()) {
        return nullptr;
    }

    try {
        data->fromJson(jobj);
    } catch (Exception &p_e) {
        Q_UNUSED(p_e);
        return nullptr;
    }

    return data;
}

const QVector<QSharedPointer<ClipboardDataItem>> &ClipboardData::getData() const
{
    return m_data;
}

ID ClipboardData::getInstanceId() const
{
    return m_instanceId;
}

ClipboardData::Action ClipboardData::getAction() const
{
    return m_action;
}
