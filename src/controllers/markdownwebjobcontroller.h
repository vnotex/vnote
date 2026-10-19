#ifndef MARKDOWNWEBJOBCONTROLLER_H
#define MARKDOWNWEBJOBCONTROLLER_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTextCharFormat>
#include <QVector>

#include <functional>
#include <memory>

#include <core/global.h>
#include <core/services/buffer2.h>
#include <gui/services/webviewjobs.h>

namespace vnotex {
class ServiceLocator;

// Per-editor policy and typed results. This controller never creates a view or owns a profile.
class MarkdownWebJobController : public QObject {
  Q_OBJECT
public:
  struct PreviewData {
    PreviewData() = default;
    PreviewData(quint64 p_id, TimeStamp p_timeStamp, const QString &p_format,
                const QByteArray &p_data, bool p_needScale, const QSize &p_logicalSize = QSize())
        : m_id(p_id), m_timeStamp(p_timeStamp), m_format(p_format), m_data(p_data),
          m_needScale(p_needScale), m_logicalSize(p_logicalSize) {}

    quint64 m_id = 0;
    quint64 m_timeStamp = 0;
    QString m_format;
    QByteArray m_data;
    bool m_needScale = false;
    QSize m_logicalSize;
  };

  struct CssRuleStyle {
    QTextCharFormat toTextCharFormat() const;
    static CssRuleStyle fromJson(const QJsonObject &p_obj);
    QString m_selector;
    QString m_color;
    QString m_backgroundColor;
    QString m_fontWeight;
    QString m_fontStyle;
  };

  struct HeadingAnchorResult {
    bool m_found = false;
    QString m_anchor;
    WebViewJobResult::Status m_status = WebViewJobResult::Status::Error;
    QString m_error;
  };

  MarkdownWebJobController(ServiceLocator &p_services, const Buffer2 &p_buffer,
                           QObject *p_parent = nullptr);
  ~MarkdownWebJobController() override;

  void setPreferredSession(WebViewJobSession *p_session,
                           const QByteArray &p_protectedHighlightCss = {});
  void setProtectedSession(WebViewJobSession *p_session, const QByteArray &p_highlightCss);
  void setRenderContext(qreal p_devicePixelRatio, qreal p_width);
  void setSuspended(bool p_suspended);
  void invalidate();
  void revoke();

  void previewGraph(quint64 p_id, TimeStamp p_timeStamp, const QString &p_language,
                    const QString &p_text, qreal p_scale);
  void previewMath(quint64 p_id, TimeStamp p_timeStamp, const QString &p_text, qreal p_scale);
  void highlightCodeBlock(int p_idx, quint64 p_timeStamp, const QString &p_text);
  void highlightMath(int p_idx, quint64 p_timeStamp, const QString &p_text);
  void htmlToMarkdown(quint64 p_id, quint64 p_timeStamp, const QString &p_html);
  void fetchHeadingAnchor(const QString &p_text, int p_lineNumber,
                          std::function<void(const HeadingAnchorResult &)> p_callback);
  void fetchHighlightStyles(std::function<void(const QVector<CssRuleStyle> *)> p_callback);

signals:
  void graphPreviewDataReady(const PreviewData &p_data);
  void mathPreviewDataReady(const PreviewData &p_data);
  void highlightCodeBlockReady(int p_idx, quint64 p_timeStamp, const QString &p_html);
  void highlightMathReady(int p_idx, quint64 p_timeStamp, const QString &p_html);
  void htmlToMarkdownReady(quint64 p_id, quint64 p_timeStamp, const QString &p_text);
  // Explicit user actions receive failure here, never a fabricated empty success.
  void operationFailed(const QString &p_operation, quint64 p_id, quint64 p_timeStamp,
                       const QString &p_error);
  void ready();
  void protectedSessionRequested();
  void protectedSessionReleaseRequested();

private slots:
  void onProtectedLockingChanged(bool p_locking);

private:
  struct Request;
  using Completion = WebViewJobSession::Completion;
  bool contextIsCurrent() const;
  bool acceptsWebWork() const;
  bool authorizeSession(WebViewJobSession *p_session);
  bool preferredIsCurrent() const;
  void observeSession(WebViewJobSession *p_session);
  WebViewJobSession *selectSession();
  void notifyReadyEndpoint();
  void releaseDrainedProtectedSession();
  void submit(const QString &p_operation, QJsonObject p_arguments, Completion p_completion,
              const QString &p_automaticKey = {}, quint64 p_timeStamp = 0,
              bool p_styleSheet = false);
  void dispatch(quint64 p_request);
  void finish(quint64 p_request, const WebViewJobResult &p_result);
  void cancel(quint64 p_request);
  void cancelAll();
  void preview(bool p_math, quint64 p_id, TimeStamp p_timeStamp, const QString &p_language,
               const QString &p_text, qreal p_scale);
  void highlight(bool p_math, int p_idx, quint64 p_timeStamp, const QString &p_text);

  ServiceLocator &m_services;
  const Buffer2 &m_buffer;
  QPointer<WebViewJobSession> m_preferred;
  QPointer<WebViewJobSession> m_shared;
  QPointer<WebViewJobSession> m_readyEndpoint;
  QString m_readyEndpointGeneration;
  QPointer<WebViewJobSession> m_protected;
  QHash<WebViewJobSession *, QMetaObject::Connection> m_observed;
  QString m_preferredGeneration;
  quint64 m_preferredEpoch = 0;
  QByteArray m_preferredCss;
  QByteArray m_protectedCss;
  QHash<quint64, std::shared_ptr<Request>> m_requests;
  QHash<QString, quint64> m_automaticRequests;
  QHash<QString, quint64> m_automaticStamps;
  quint64 m_nextRequest = 0;
  quint64 m_generation = 1;
  QString m_scopePrefix;
  QString m_scope;
  qreal m_devicePixelRatio = 1.0;
  qreal m_width = 800.0;
  bool m_protectedRequested = false;
  bool m_protectedRetiring = false;
  bool m_suspended = false;
  bool m_locked = false;
  bool m_revoked = false;
  bool m_destroying = false;
  int m_closeHook = -1;
  int m_notebookCloseHook = -1;
};
} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::MarkdownWebJobController::PreviewData)

#endif // MARKDOWNWEBJOBCONTROLLER_H
