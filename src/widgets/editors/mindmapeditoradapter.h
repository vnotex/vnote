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

        // Signals to be connected at web side.
    signals:
        void dataUpdated(const QString& p_data);

        void saveDataRequested(quint64 p_id);

    signals:
        void contentsChanged();

    private:
    };
}

#endif // MINDMAPEDITORADAPTER_H
