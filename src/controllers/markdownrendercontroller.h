#ifndef MARKDOWNRENDERCONTROLLER_H
#define MARKDOWNRENDERCONTROLLER_H

#include <QObject>
#include <QString>

#include <deque>
#include <functional>
#include <memory>

#include <utils/markdownhtmlrenderer.h>

namespace vnotex {
class Buffer2;
class ProtectedBufferLease;
class ServiceLocator;

// Per-view native jobs on Qt's shared pool. All public operations run on the owning thread.
class MarkdownRenderController : public QObject {
  Q_OBJECT
public:
  struct HeadingAnchorResult {
    bool found = false;
    QString anchor;
    QString error;
    bool cancelled = false;
  };

  MarkdownRenderController(ServiceLocator &p_services, const Buffer2 &p_buffer,
                           QObject *p_parent = nullptr);
  ~MarkdownRenderController() override;

  QString render(const QByteArray &p_source, const MarkdownHtmlOptions &p_options,
                 const vte::MarkdownDocumentPtr &p_document = {});
  void fetchHeadingAnchor(const QByteArray &p_source, int p_line,
                          const MarkdownHtmlOptions &p_options,
                          const vte::MarkdownDocumentPtr &p_document,
                          std::function<void(const HeadingAnchorResult &)> p_callback);

  void invalidate();
  void setSuspended(bool p_suspended);
  void revoke();
  bool isCurrent(const QString &p_token) const;
  // Includes cancelled workers until their inputs/results have actually been released.
  bool isBusy() const;

signals:
  void rendered(const QString &p_token, const MarkdownHtmlResult &p_result);
  // Observe this before final protected clearance; cancellation itself never waits for cmark.
  void drained();

private slots:
  void onProtectedLockingChanged(bool p_locking);

private:
  struct Input;
  struct PreviewRequest;
  struct HeadingRequest;

  bool contextIsCurrent(std::shared_ptr<ProtectedBufferLease> *p_lease = nullptr) const;
  bool acceptsWork(std::shared_ptr<ProtectedBufferLease> *p_lease = nullptr) const;
  void startPreview();
  void startHeading();
  void finishPreview(MarkdownHtmlResult p_result, std::shared_ptr<ProtectedBufferLease> p_lease);
  void finishHeading(MarkdownHtmlResult p_result, std::shared_ptr<ProtectedBufferLease> p_lease);
  void notifyDrained();

  ServiceLocator &m_services;
  const Buffer2 &m_buffer;
  QString m_tokenPrefix;
  QString m_currentToken;
  quint64 m_nextToken = 0;
  std::unique_ptr<PreviewRequest> m_previewActive;
  std::unique_ptr<PreviewRequest> m_previewPending;
  std::unique_ptr<HeadingRequest> m_headingActive;
  std::deque<std::unique_ptr<HeadingRequest>> m_headingsPending;
  int m_deliveries = 0;
  bool m_wasBusy = false;
  bool m_suspended = false;
  bool m_locked = false;
  bool m_revoked = false;
  bool m_destroying = false;
  int m_closeHook = -1;
  int m_notebookCloseHook = -1;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::MarkdownRenderController::HeadingAnchorResult)

#endif // MARKDOWNRENDERCONTROLLER_H
