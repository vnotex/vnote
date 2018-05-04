#include "vmathjaxpreviewhelper.h"

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
    Q_ASSERT(m_parentWidget);

    m_initialized = true;

    m_webView = new QWebEngineView(m_parentWidget);
    connect(m_webView, &QWebEngineView::loadFinished,
            this, [this]() {
                m_webReady = true;
                for (auto const & it : m_pendingFunc) {
                    it();
                }

                m_pendingFunc.clear();
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

    // setHtml() will change focus if it is not disabled.
    m_webView->setEnabled(false);
    m_webView->setHtml(VUtils::generateMathJaxPreviewTemplate(), QUrl("qrc:/resources"));
    m_webView->setEnabled(true);
}

void VMathJaxPreviewHelper::previewMathJax(int p_identifier,
                                           int p_id,
                                           TimeStamp p_timeStamp,
                                           const QString &p_text)
{
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
