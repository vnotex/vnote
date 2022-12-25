#include "mindmapeditoradapter.h"

using namespace vnotex;

MindMapEditorAdapter::MindMapEditorAdapter(QObject *p_parent)
    : WebViewAdapter(p_parent)
{
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
