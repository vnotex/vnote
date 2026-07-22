#ifndef WEBVIEWER_H
#define WEBVIEWER_H

#include <QWebEngineView>

#include <core/global.h>

namespace vnotex {
class WebViewer : public QWebEngineView {
  Q_OBJECT
public:
  WebViewer(const QColor &p_background, qreal p_zoomFactor, QWidget *p_parent = nullptr);

  WebViewer(const QColor &p_background, QWidget *p_parent = nullptr);

  virtual ~WebViewer();

  void findText(const QString &p_text, FindOptions p_options);

signals:
  void linkHovered(const QString &p_url);

  void localFileOpenRequested(const QUrl &p_url);

  // Emitted for external (non-local) main-frame link navigations when a consumer
  // is connected. When there is no consumer, the viewer falls back to opening the
  // link with the system (WidgetUtils::openUrlByDesktop).
  void externalLinkRequested(const QUrl &p_url);

private slots:
  void handleExternalLinkRequested(const QUrl &p_url);
};
} // namespace vnotex

#endif // WEBVIEWER_H
