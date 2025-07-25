#include "mindmapeditoradapter.h"

using namespace vnotex;

MindMapEditorAdapter::MindMapEditorAdapter(QObject *p_parent)
    : WebViewAdapter(p_parent)
{
    qDebug() << "MindMapEditorAdapter: Constructor called";
}

void MindMapEditorAdapter::setData(const QString &p_data)
{
    if (isReady()) {
        emit dataUpdated(p_data);
    } else {
        pendAction(std::bind(&MindMapEditorAdapter::setData, this, p_data));
    }
}

void MindMapEditorAdapter::saveData(const std::function<void(const QString &)> &p_callback)
{
    if (isReady()) {
        const quint64 id = addCallback([p_callback](void *data) {
            p_callback(*reinterpret_cast<const QString *>(data));
        });
        emit saveDataRequested(id);
    } else {
        pendAction(std::bind(&MindMapEditorAdapter::saveData, this, p_callback));
    }
}

void MindMapEditorAdapter::setSavedData(quint64 p_id, const QString &p_data)
{
    invokeCallback(p_id, (void *)&p_data);
}

void MindMapEditorAdapter::notifyContentsChanged()
{
    emit contentsChanged();
}

void MindMapEditorAdapter::urlClicked(const QString &p_url)
{
    if (p_url.isEmpty()) {
        qWarning() << "MindMapEditorAdapter::urlClicked: URL is empty";
        return;
    }

    qDebug() << "MindMapEditorAdapter::urlClicked: Emitting urlClickRequested signal with URL:" << p_url;
    
    emit urlClickRequested(p_url);
}

void MindMapEditorAdapter::urlClickedWithDirection(const QString &p_url, const QString &p_direction)
{
    if (p_url.isEmpty()) {
        qWarning() << "MindMapEditorAdapter::urlClickedWithDirection: URL is empty";
        return;
    }

    qDebug() << "MindMapEditorAdapter::urlClickedWithDirection: URL:" << p_url << "Direction:" << p_direction;
    
    emit urlClickWithDirectionRequested(p_url, p_direction);
}
