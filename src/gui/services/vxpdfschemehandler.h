#ifndef VXPDFSCHEMEHANDLER_H
#define VXPDFSCHEMEHANDLER_H

#include <functional>

#include <QHash>
#include <QObject>
#include <QString>

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)

#include <QWebEngineUrlSchemeHandler>

class QWebEngineUrlRequestJob;

namespace vnotex {

class ConfigMgr2;

// Serves the `vxpdf://` scheme. See src/core/vxpdfscheme.h for the URL contract.
//
// Dependencies are explicit constructor arguments (no globals):
//   * ConfigMgr2 resolves a config-folder-relative asset path to an absolute one.
//   * the template accessor hands back the CURRENT generated PDF viewer template,
//     which HtmlTemplateService regenerates on config and theme changes.
class VxPdfSchemeHandler : public QWebEngineUrlSchemeHandler {
  Q_OBJECT

public:
  using TemplateAccessor = std::function<QString()>;

  VxPdfSchemeHandler(ConfigMgr2 *p_configMgr, TemplateAccessor p_templateAccessor,
                     QObject *p_parent = nullptr);

  void requestStarted(QWebEngineUrlRequestJob *p_job) override;

  // Registers @p_absPath and returns an opaque token usable in a
  // `vxpdf://pdf/document/<token>` URL. Returns an empty string for an empty path.
  QString registerDocument(const QString &p_absPath);

  void unregisterDocument(const QString &p_token);

  bool hasDocument(const QString &p_token) const;

  // Registers the `vxpdf` scheme with QtWebEngine. MUST be called before the
  // QApplication is constructed; calling it twice is harmless (Qt warns).
  static void registerScheme();

  // Exposed for tests: the MIME type served for a given path.
  static QByteArray mimeTypeForPath(const QString &p_path);

private:
  void serveFile(QWebEngineUrlRequestJob *p_job, const QString &p_absPath,
                 const QByteArray &p_mimeType) const;

  void serveTemplate(QWebEngineUrlRequestJob *p_job) const;

  ConfigMgr2 *m_configMgr = nullptr;

  TemplateAccessor m_templateAccessor;

  // token -> absolute PDF path.
  QHash<QString, QString> m_documents;
};

} // namespace vnotex

#endif // QT_VERSION >= 6.9.0

#endif // VXPDFSCHEMEHANDLER_H
