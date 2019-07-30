#include "vmathjaxpreviewhelper.h"

#include <QWebView>
#include <QWebChannel>
#include <QWebSocketServer>

#include "utils/vutils.h"
#include "vmathjaxwebdocument.h"
#include "vconfigmanager.h"
#include "websocketclientwrapper.h"
#include "websockettransport.h"
#include "vconstants.h"

extern VConfigManager *g_config;

VMathJaxPreviewHelper::VMathJaxPreviewHelper(QWidget *p_parentWidget, QObject *p_parent)
    : QObject(p_parent),
      m_parentWidget(p_parentWidget),
      m_initialized(false),
      m_nextID(0),
      m_webView(NULL),
      m_channel(nullptr),
      m_webReady(false)
{
}

VMathJaxPreviewHelper::~VMathJaxPreviewHelper()
{
}

void VMathJaxPreviewHelper::doInit()
{
    Q_ASSERT(!m_initialized);
    Q_ASSERT(m_parentWidget);

    m_initialized = true;

    // FIXME.
    return;

    m_webView = new QWebView(m_parentWidget);
    connect(m_webView, &QWebView::loadFinished,
            this, [this]() {
                m_webReady = true;
                for (auto const & it : m_pendingFunc) {
                    it();
                }

                m_pendingFunc.clear();
            });
    m_webView->page()->settings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);
    m_webView->hide();
    m_webView->setFocusPolicy(Qt::NoFocus);

    m_webDoc = new VMathJaxWebDocument(m_webView);
    connect(m_webDoc, &VMathJaxWebDocument::mathjaxPreviewResultReady,
            this, [this](int p_identifier,
                         int p_id,
                         TimeStamp p_timeStamp,
                         const QString &p_format,
                         const QString &p_data) {
                QByteArray ba = QByteArray::fromBase64(p_data.toUtf8());
                emit mathjaxPreviewResultReady(p_identifier, p_id, p_timeStamp, p_format, ba);
            });

    connect(m_webDoc, &VMathJaxWebDocument::diagramPreviewResultReady,
            this, [this](int p_identifier,
                        int p_id,
                        TimeStamp p_timeStamp,
                        const QString &p_format,
                        const QString &p_data) {
                QByteArray ba;
                if (p_format == "png") {
                    ba = QByteArray::fromBase64(p_data.toUtf8());
                } else {
                    ba = p_data.toUtf8();
                }

                emit diagramPreviewResultReady(p_identifier, p_id, p_timeStamp, p_format, ba);
            });

    quint16 port = WebSocketPort::PreviewHelperPort;
    bindToChannel(port, "content", m_webDoc);

    // setHtml() will change focus if it is not disabled.
    m_webView->setEnabled(false);
    QUrl baseUrl(QUrl::fromLocalFile(g_config->getDocumentPathOrHomePath() + QDir::separator()));
    m_webView->setHtml(VUtils::generateMathJaxPreviewTemplate(port), baseUrl);
    m_webView->setEnabled(true);
}

void VMathJaxPreviewHelper::previewMathJax(int p_identifier,
                                           int p_id,
                                           TimeStamp p_timeStamp,
                                           const QString &p_text)
{
    emit mathjaxPreviewResultReady(p_identifier, p_id, p_timeStamp, "png", "");
    return;

    init();

    if (!m_webReady) {
        auto func = std::bind(&VMathJaxWebDocument::previewMathJax,
                              m_webDoc,
                              p_identifier,
                              p_id,
                              p_timeStamp,
                              p_text,
                              false);
        m_pendingFunc.append(func);
    } else {
        m_webDoc->previewMathJax(p_identifier, p_id, p_timeStamp, p_text, false);
    }
}

void VMathJaxPreviewHelper::previewMathJaxFromHtml(int p_identifier,
                                                   int p_id,
                                                   TimeStamp p_timeStamp,
                                                   const QString &p_html)
{
    emit mathjaxPreviewResultReady(p_identifier, p_id, p_timeStamp, "png", "");
    return;

    init();

    if (!m_webReady) {
        auto func = std::bind(&VMathJaxWebDocument::previewMathJax,
                              m_webDoc,
                              p_identifier,
                              p_id,
                              p_timeStamp,
                              p_html,
                              true);
        m_pendingFunc.append(func);
    } else {
        m_webDoc->previewMathJax(p_identifier, p_id, p_timeStamp, p_html, true);
    }
}

void VMathJaxPreviewHelper::previewDiagram(int p_identifier,
                                           int p_id,
                                           TimeStamp p_timeStamp,
                                           const QString &p_lang,
                                           const QString &p_text)
{
    emit diagramPreviewResultReady(p_identifier, p_id, p_timeStamp, "png", "");
    return;

    init();

    if (!m_webReady) {
        auto func = std::bind(&VMathJaxWebDocument::previewDiagram,
                              m_webDoc,
                              p_identifier,
                              p_id,
                              p_timeStamp,
                              p_lang,
                              p_text);
        m_pendingFunc.append(func);
    } else {
        m_webDoc->previewDiagram(p_identifier, p_id, p_timeStamp, p_lang, p_text);
    }
}

void VMathJaxPreviewHelper::bindToChannel(quint16 p_port, const QString &p_name, QObject *p_object)
{
    Q_ASSERT(!m_channel);
    auto server = new QWebSocketServer("Web View for Preview",
                                       QWebSocketServer::NonSecureMode,
                                       this);
    quint16 port = p_port;
    if (!server->listen(QHostAddress::LocalHost, port)) {
        qWarning() << "fail to open web socket server on port" << port;
        delete server;
        return;
    }

    auto clientWrapper = new WebSocketClientWrapper(server, this);
    m_channel = new QWebChannel(this);
    connect(clientWrapper, &WebSocketClientWrapper::clientConnected,
            m_channel, &QWebChannel::connectTo);
    m_channel->registerObject(p_name, p_object);
}
