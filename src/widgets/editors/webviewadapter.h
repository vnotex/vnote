#ifndef WEBVIEWADAPTER_H
#define WEBVIEWADAPTER_H

#include <QObject>

#include <QVector>

#include <utils/callbackpool.h>
#include <core/global.h>

namespace vnotex
{
    // Base class of adapter and interface between CPP and JS for WebView.
    class WebViewAdapter : public QObject
    {
        Q_OBJECT
    public:
        explicit WebViewAdapter(QObject *p_parent = nullptr);

        bool isReady() const;

        void findText(const QStringList &p_texts, FindOptions p_options, int p_currentMatchLine = -1);

        // Functions to be called from web side.
    public slots:
        void setReady(bool p_ready);

        void setFindText(const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex);

        // Signals to be connected at cpp side.
    signals:
        void ready();

        void findTextReady(const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex);

        // Signals to be connected at web side.
    signals:
        void findTextRequested(const QStringList &p_texts, const QJsonObject &p_options, int p_currentMatchLine);

    protected:
        void pendAction(const std::function<void()> &p_func);

        void invokeCallback(quint64 p_id, void *p_data);

        quint64 addCallback(const CallbackPool::Callback &p_callback);

    private:
        struct FindOption
        {
            QJsonObject toJson() const;

            bool m_findBackward = false;

            bool m_caseSensitive = false;

            bool m_wholeWordOnly = false;

            bool m_regularExpression = false;
        };

        // Whether web side is ready.
        bool m_ready = false;

        // Pending actions for the editor once it is ready.
        QVector<std::function<void()>> m_pendingActions;

        CallbackPool m_callbackPool;
    };
}

#endif // WEBVIEWADAPTER_H
