#include "vmathjaxpreviewhelper.h"

#include <QDebug>
#include <QWebEngineView>
#include <QWebChannel>

#include "utils/vutils.h"
#include "vmathjaxwebdocument.h"

VMathJaxPreviewHelper::VMathJaxPreviewHelper(QWidget *p_parentWidget, QObject *p_parent)
    : QObject(p_parent),
      m_parentWidget(p_parentWidget),
      m_initialized(false),
      m_nextID(0),
      m_webView(NULL),
      m_webReady(false)
{
}

VMathJaxPreviewHelper::~VMathJaxPreviewHelper()
{
}

void VMathJaxPreviewHelper::doInit()
{
    Q_ASSERT(!m_initialized);
    m_initialized = true;

    m_webView = new QWebEngineView(m_parentWidget);
    connect(m_webView, &QWebEngineView::loadFinished,
            this, [this]() {
                m_webReady = true;
            });

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
                QByteArray ba = QByteArray::fromBase64(p_data.toUtf8());
                emit diagramPreviewResultReady(p_identifier, p_id, p_timeStamp, p_format, ba);
            });

    QWebChannel *channel = new QWebChannel(m_webView);
    channel->registerObject(QStringLiteral("content"), m_webDoc);
    m_webView->page()->setWebChannel(channel);

    m_webView->setHtml(VUtils::generateMathJaxPreviewTemplate(), QUrl("qrc:/resources"));

    while (!m_webReady) {
        VUtils::sleepWait(100);
    }
}

void VMathJaxPreviewHelper::previewMathJax(int p_identifier,
                                           int p_id,
                                           TimeStamp p_timeStamp,
                                           const QString &p_text)
{
    init();

    m_webDoc->previewMathJax(p_identifier, p_id, p_timeStamp, p_text);
}

void VMathJaxPreviewHelper::previewDiagram(int p_identifier,
                                           int p_id,
                                           TimeStamp p_timeStamp,
                                           const QString &p_lang,
                                           const QString &p_text)
{
    init();

    m_webDoc->previewDiagram(p_identifier, p_id, p_timeStamp, p_lang, p_text);
}
