#ifndef WEBENGINEPROFILESERVICE_H
#define WEBENGINEPROFILESERVICE_H

#include <QObject>
#include <QString>
#include <QtGlobal>

#include "core/noncopyable.h"

class QWebEngineProfile;

namespace vnotex {

class ConfigMgr2;
class HtmlTemplateService;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
class VxPdfSchemeHandler;
#endif

// Owns a named (non off-the-record) QtWebEngine profile with a disk HTTP cache, so that
// remote resources (mainly images) survive tab close and application restart.
//
// It also owns the `vxpdf://` scheme handler (Qt >= 6.9 only), because a URL scheme
// handler is installed per profile and must outlive every page using it.
//
// Lifetime: the profile must outlive every QWebEnginePage created with it. Declare this
// service before the main window so that destruction order tears the window down first.
class WebEngineProfileService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // @p_configMgr and @p_templateService may be null (tests): the vxpdf handler is
  // then not installed and the PDF viewer simply has nothing to load.
  WebEngineProfileService(const QString &p_cacheRoot, ConfigMgr2 *p_configMgr,
                          HtmlTemplateService *p_templateService, QObject *p_parent = nullptr);

  QWebEngineProfile *profile() const;

  // Registers @p_absPath with the vxpdf handler and returns the token to embed in a
  // `vxpdf://pdf/document/<token>` URL. Empty when the handler is unavailable.
  QString registerPdfDocument(const QString &p_absPath);

  void unregisterPdfDocument(const QString &p_token);

  bool hasPdfDocument(const QString &p_token) const;

  // Path derivation helpers (free of Qt WebEngine, testable).
  static QString webCachePath(const QString &p_root);

  static QString webStoragePath(const QString &p_root);

private:
  QWebEngineProfile *m_profile = nullptr;

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  VxPdfSchemeHandler *m_pdfSchemeHandler = nullptr;
#endif
};

} // namespace vnotex

#endif // WEBENGINEPROFILESERVICE_H
