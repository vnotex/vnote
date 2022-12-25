#include "webviewadapter.h"

#include <utils/utils.h>

using namespace vnotex;

QJsonObject WebViewAdapter::FindOption::toJson() const
{
    QJsonObject obj;
    obj["findBackward"] = m_findBackward;
    obj["caseSensitive"] = m_caseSensitive;
    obj["wholeWordOnly"] = m_wholeWordOnly;
    obj["regularExpression"] = m_regularExpression;
    return obj;
}

WebViewAdapter::WebViewAdapter(QObject *p_parent)
    : QObject(p_parent)
{
}

void WebViewAdapter::setReady(bool p_ready)
{
    if (m_ready == p_ready) {
        return;
    }

    m_ready = p_ready;
    if (m_ready) {
        for (auto &act : m_pendingActions) {
            act();
        }
        m_pendingActions.clear();
        emit ready();
    } else {
        m_pendingActions.clear();
    }
}

void WebViewAdapter::pendAction(const std::function<void()> &p_func)
{
    Q_ASSERT(!m_ready);
    m_pendingActions.append(p_func);
}

bool WebViewAdapter::isReady() const
{
    return m_ready;
}

void WebViewAdapter::invokeCallback(quint64 p_id, void *p_data)
{
    m_callbackPool.call(p_id, p_data);
}

quint64 WebViewAdapter::addCallback(const CallbackPool::Callback &p_callback)
{
    return m_callbackPool.add(p_callback);
}

void WebViewAdapter::findText(const QStringList &p_texts, FindOptions p_options, int p_currentMatchLine)
{
    FindOption opts;
    if (p_options & vnotex::FindOption::FindBackward) {
        opts.m_findBackward = true;
    }
    if (p_options & vnotex::FindOption::CaseSensitive) {
        opts.m_caseSensitive = true;
    }
    if (p_options & vnotex::FindOption::WholeWordOnly) {
        opts.m_wholeWordOnly = true;
    }
    if (p_options & vnotex::FindOption::RegularExpression) {
        opts.m_regularExpression = true;
    }

    if (isReady()) {
        emit findTextRequested(p_texts, opts.toJson(), p_currentMatchLine);
    } else {
        pendAction([this, p_texts, opts, p_currentMatchLine]() {
            // FIXME: highlights will be clear once the page is ready. Add a delay here.
            Utils::sleepWait(1000);
            emit findTextRequested(p_texts, opts.toJson(), p_currentMatchLine);
        });
    }
}

void WebViewAdapter::setFindText(const QStringList &p_texts, int p_totalMatches, int p_currentMatchIndex)
{
    emit findTextReady(p_texts, p_totalMatches, p_currentMatchIndex);
}

