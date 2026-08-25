#include "vxpdfschemehandler.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUuid>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

#include <core/configmgr2.h>
#include <core/vxpdfscheme.h>

using namespace vnotex;

namespace {

// Explicit MIME map. Chromium applies STRICT MIME checking to module scripts, so
// an empty or generic type on a `.mjs` fails the module load with no useful error.
QByteArray mimeForSuffix(const QString &p_suffix) {
  static const QHash<QString, QByteArray> c_map = {
      {QStringLiteral("html"), QByteArrayLiteral("text/html")},
      {QStringLiteral("htm"), QByteArrayLiteral("text/html")},
      {QStringLiteral("css"), QByteArrayLiteral("text/css")},
      {QStringLiteral("mjs"), QByteArrayLiteral("text/javascript")},
      {QStringLiteral("js"), QByteArrayLiteral("text/javascript")},
      {QStringLiteral("json"), QByteArrayLiteral("application/json")},
      {QStringLiteral("pdf"), QByteArrayLiteral("application/pdf")},
      {QStringLiteral("svg"), QByteArrayLiteral("image/svg+xml")},
      {QStringLiteral("png"), QByteArrayLiteral("image/png")},
      {QStringLiteral("gif"), QByteArrayLiteral("image/gif")},
      {QStringLiteral("jpg"), QByteArrayLiteral("image/jpeg")},
      {QStringLiteral("jpeg"), QByteArrayLiteral("image/jpeg")},
      {QStringLiteral("webp"), QByteArrayLiteral("image/webp")},
      {QStringLiteral("wasm"), QByteArrayLiteral("application/wasm")},
      {QStringLiteral("ttf"), QByteArrayLiteral("font/ttf")},
      {QStringLiteral("otf"), QByteArrayLiteral("font/otf")},
      {QStringLiteral("woff"), QByteArrayLiteral("font/woff")},
      {QStringLiteral("woff2"), QByteArrayLiteral("font/woff2")},
      {QStringLiteral("pfb"), QByteArrayLiteral("application/octet-stream")},
      {QStringLiteral("icc"), QByteArrayLiteral("application/octet-stream")},
      // Fluent localization resources and the CMap tables.
      {QStringLiteral("ftl"), QByteArrayLiteral("text/plain")},
      {QStringLiteral("bcmap"), QByteArrayLiteral("application/octet-stream")}};

  return c_map.value(p_suffix, QByteArrayLiteral("application/octet-stream"));
}

} // namespace

VxPdfSchemeHandler::VxPdfSchemeHandler(ConfigMgr2 *p_configMgr, TemplateAccessor p_templateAccessor,
                                       QObject *p_parent)
    : QWebEngineUrlSchemeHandler(p_parent), m_configMgr(p_configMgr),
      m_templateAccessor(std::move(p_templateAccessor)) {}

void VxPdfSchemeHandler::registerScheme() {
  QWebEngineUrlScheme scheme(VxPdfScheme::scheme().toUtf8());
  // Syntax::Host gives the scheme a stable authority, which is what makes the
  // same-origin policy (and therefore ES module loading) work.
  scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
  scheme.setDefaultPort(QWebEngineUrlScheme::PortUnspecified);
  // Validated by the Phase 0 spike: SecureScheme | CorsEnabled | FetchApiAllowed is
  // sufficient for ESM + the pdf.worker.mjs web worker + the `?file=` document fetch.
  // LocalAccessAllowed is deliberately NOT set: nothing the viewer loads is a file: URL.
  scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled |
                  QWebEngineUrlScheme::FetchApiAllowed);
  QWebEngineUrlScheme::registerScheme(scheme);
}

QByteArray VxPdfSchemeHandler::mimeTypeForPath(const QString &p_path) {
  return mimeForSuffix(QFileInfo(p_path).suffix().toLower());
}

QString VxPdfSchemeHandler::registerDocument(const QString &p_absPath) {
  if (p_absPath.isEmpty()) {
    return QString();
  }

  const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_documents.insert(token, p_absPath);
  return token;
}

void VxPdfSchemeHandler::unregisterDocument(const QString &p_token) { m_documents.remove(p_token); }

bool VxPdfSchemeHandler::hasDocument(const QString &p_token) const {
  return m_documents.contains(p_token);
}

void VxPdfSchemeHandler::requestStarted(QWebEngineUrlRequestJob *p_job) {
  const auto url = p_job->requestUrl();
  // All routing/rejection logic lives in VxPdfScheme::routeFor() so it can be
  // table-tested without a QWebEngineUrlRequestJob (which has no public ctor).
  const auto route = VxPdfScheme::routeFor(p_job->requestMethod(), url.host(), url.path());

  switch (route.m_kind) {
  case VxPdfScheme::Route::Kind::DenyMethod:
    p_job->fail(QWebEngineUrlRequestJob::RequestDenied);
    return;

  case VxPdfScheme::Route::Kind::DenyPath:
    qWarning() << "VxPdfSchemeHandler: rejected path" << url.path();
    p_job->fail(QWebEngineUrlRequestJob::RequestDenied);
    return;

  case VxPdfScheme::Route::Kind::NotFound:
    p_job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    return;

  case VxPdfScheme::Route::Kind::Document: {
    const auto absPath = m_documents.value(route.m_target);
    if (absPath.isEmpty()) {
      p_job->fail(QWebEngineUrlRequestJob::UrlNotFound);
      return;
    }
    serveFile(p_job, absPath, QByteArrayLiteral("application/pdf"));
    return;
  }

  case VxPdfScheme::Route::Kind::ViewerTemplate:
    serveTemplate(p_job);
    return;

  case VxPdfScheme::Route::Kind::Asset:
    if (!m_configMgr) {
      p_job->fail(QWebEngineUrlRequestJob::RequestFailed);
      return;
    }
    const auto absPath = m_configMgr->getFileFromConfigFolder(route.m_target);
    serveFile(p_job, absPath, mimeTypeForPath(absPath));
    return;
  }
}

void VxPdfSchemeHandler::serveTemplate(QWebEngineUrlRequestJob *p_job) const {
  const QString html = m_templateAccessor ? m_templateAccessor() : QString();
  if (html.isEmpty()) {
    p_job->fail(QWebEngineUrlRequestJob::RequestFailed);
    return;
  }

  // Parented to the job so the device outlives the reply; a stack-local
  // QBuffer would be destroyed before Chromium finished reading it.
  auto *buffer = new QBuffer(p_job);
  buffer->setData(html.toUtf8());
  if (!buffer->open(QIODevice::ReadOnly)) {
    p_job->fail(QWebEngineUrlRequestJob::RequestFailed);
    return;
  }
  p_job->reply(QByteArrayLiteral("text/html"), buffer);
}

void VxPdfSchemeHandler::serveFile(QWebEngineUrlRequestJob *p_job, const QString &p_absPath,
                                   const QByteArray &p_mimeType) const {
  auto *file = new QFile(p_absPath, p_job);
  if (!file->open(QIODevice::ReadOnly)) {
    qWarning() << "VxPdfSchemeHandler: cannot open" << p_absPath;
    p_job->fail(QWebEngineUrlRequestJob::UrlNotFound);
    return;
  }
  p_job->reply(p_mimeType, file);
}

#endif // QT_VERSION >= 6.9.0
