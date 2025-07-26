#ifndef MINDMAPEDITORADAPTER_H
#define MINDMAPEDITORADAPTER_H

#include "webviewadapter.h"

#include <QString>
#include <QJsonObject>

#include <core/global.h>

namespace vnotex
{
    // Adapter and interface between CPP and JS for MindMap.
    class MindMapEditorAdapter : public WebViewAdapter
    {
        Q_OBJECT
    public:
        explicit MindMapEditorAdapter(QObject *p_parent = nullptr);

        ~MindMapEditorAdapter() = default;

        void setData(const QString &p_data);

        void saveData(const std::function<void(const QString &)> &p_callback);

        // Functions to be called from web side.
    public slots:
        void setSavedData(quint64 p_id, const QString &p_data);

        void notifyContentsChanged();

        // 处理来自JavaScript的URL点击事件
        void urlClicked(const QString &p_url);

        // 处理来自JavaScript的带方向的URL点击事件
        void urlClickedWithDirection(const QString &p_url, const QString &p_direction);

        // Signals to be connected at web side.
    signals:
        void dataUpdated(const QString& p_data);

        void saveDataRequested(quint64 p_id);

    signals:
        void contentsChanged();

        // 发出URL点击信号，供其他组件处理
        void urlClickRequested(const QString &p_url);

        // 发出带方向的URL点击信号
        void urlClickWithDirectionRequested(const QString &p_url, const QString &p_direction);

    private:
    };
}

#endif // MINDMAPEDITORADAPTER_H
