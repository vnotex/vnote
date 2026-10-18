#include "markdownwebjobhost.h"

#include <QDir>
#include <QVariant>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/htmltemplateservice.h>
#include <gui/services/themeservice.h>
#include <gui/services/webengineprofileservice.h>
#include <gui/services/webviewjobs.h>
#include <widgets/webpage.h>
#include <widgets/webviewer.h>

#include "markdownvieweradapter.h"

using namespace vnotex;

namespace {
void validateRuntimeOnLoad(QWebEnginePage *p_page, WebViewJobSession *p_session) {
  QObject::connect(
      p_page, &QWebEnginePage::loadFinished, p_session, [p_page, p_session](bool p_ok) {
        if (!p_ok)
          return; // Generic session owns navigation errors.
        const QPointer<WebViewJobSession> session(p_session);
        const auto generation = p_session->generation();
        p_page->runJavaScript(
            QStringLiteral("Boolean(window.vxMarkdownJobsRegistered && window.vxcore && "
                           "window.vxcore.initialized && window.vxWebJobs)"),
            [session, generation](const QVariant &p_available) {
              if (session && session->generation() == generation && !p_available.toBool()) {
                session->fail(QStringLiteral("Markdown job runtime failed to initialize"));
              }
            });
      });
}
} // namespace

struct MarkdownWebJobHost::ProtectedRuntime {
  WebEngineProfileService::ProtectedPage profile;
  WebViewer *view = nullptr;
  WebViewJobSession *session = nullptr;
  QByteArray highlightCss;
};

MarkdownWebJobHost::MarkdownWebJobHost(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                       QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_buffer(p_buffer) {
  if (auto *buffers = m_services.get<BufferService>()) {
    connect(buffers->asQObject(), SIGNAL(protectedLockingChanged(bool)), this,
            SLOT(onProtectedLockingChanged(bool)), Qt::DirectConnection);
  }
  if (auto *hooks = m_services.get<HookManager>()) {
    m_closeHook = hooks->addAction<ViewWindowCloseEvent>(
        HookNames::ViewWindowAfterClose,
        [this](HookContext &, const ViewWindowCloseEvent &p_event) {
          if (p_event.bufferId == m_buffer.id() && !m_buffer.isValid()) {
            releaseProtectedSession();
          }
        });
    m_notebookCloseHook = hooks->addAction<NotebookCloseEvent>(
        HookNames::NotebookAfterClose, [this](HookContext &, const NotebookCloseEvent &p_event) {
          if (p_event.notebookId == m_buffer.nodeId().notebookId) {
            releaseProtectedSession();
          }
        });
  }
}

MarkdownWebJobHost::~MarkdownWebJobHost() {
  releaseProtectedSession();
  if (auto *hooks = m_services.get<HookManager>()) {
    hooks->removeAction(m_closeHook);
    hooks->removeAction(m_notebookCloseHook);
  }
}

WebViewService::Bootstrap MarkdownWebJobHost::createSharedRuntime(ServiceLocator &p_services) {
  // Only the application-lifetime registry is captured, never a requesting tab or Buffer2.
  return [&p_services](QWebEnginePage *p_page, QWebChannel *p_channel) {
    auto *session = qobject_cast<WebViewJobSession *>(
        p_channel->registeredObjects().value(QStringLiteral("vxJobs")));
    auto *templates = p_services.get<HtmlTemplateService>();
    auto *theme = p_services.get<ThemeService>();
    auto *config = p_services.get<ConfigMgr2>();
    if (!templates || !theme || !config) {
      session->fail(QStringLiteral("Markdown runtime services are unavailable"));
      return;
    }
    const auto &markdown = config->getEditorConfig().getMarkdownEditorConfig();
    templates->updateMarkdownViewerTemplate(markdown, theme->fetchWebStyleSheet(),
                                            theme->getFile(Theme::File::HighlightStyleSheet));
    auto *adapter = new MarkdownViewerAdapter(p_services, p_page);
    p_channel->registerObject(QStringLiteral("vxAdapter"), adapter);
    if (templates->getMarkdownViewerTemplate().isEmpty()) {
      session->fail(QStringLiteral("Markdown runtime template is unavailable"));
      return;
    }
    validateRuntimeOnLoad(p_page, session);
    p_page->setBackgroundColor(theme->getBaseBackground());
    p_page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    p_page->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    p_page->settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    // All scripts come from the existing template. No note URL or document source enters here.
    p_page->setHtml(templates->getMarkdownViewerTemplate(), QUrl::fromLocalFile(QDir::rootPath()));
  };
}

void MarkdownWebJobHost::ensureProtectedSession() {
  if (!m_buffer.isEncrypted() || !m_buffer.isValid() || !m_buffer.acquireProtectedLease()) {
    releaseProtectedSession();
    emit protectedSessionReady(nullptr, {});
    return;
  }
  if (m_protected) {
    emit protectedSessionReady(m_protected->session, m_protected->highlightCss);
    return;
  }
  auto *profiles = m_services.get<WebEngineProfileService>();
  auto *templates = m_services.get<HtmlTemplateService>();
  auto *theme = m_services.get<ThemeService>();
  if (!profiles || !templates || !theme) {
    emit protectedSessionReady(nullptr, {});
    return;
  }
  m_protected = std::make_unique<ProtectedRuntime>();
  auto &runtime = *m_protected;
  runtime.profile = profiles->createProtectedProfile(m_buffer, this);
  if (!runtime.profile.profile) {
    m_protected.reset();
    emit protectedSessionReady(nullptr, {});
    return;
  }
  QColor background(theme->optionalPaletteColor(
      QStringLiteral("base#content#bg"))); // palette-token-optional: custom themes may omit this
  if (!background.isValid()) {
    background = theme->getBaseBackground();
  }
  QHash<QString, QByteArray> resources;
  const auto html = templates->protectedMarkdownViewerTemplate(
      runtime.profile.token, runtime.profile.nonce, theme->getCurrentTheme().name(),
      qGray(background.rgb()) < 128, resources);
  for (auto it = resources.cbegin(); it != resources.cend(); ++it) {
    if (it.key().startsWith(QStringLiteral("/app/themes/")) &&
        it.key().endsWith(QStringLiteral("/highlight.css"))) {
      runtime.highlightCss = it.value();
      break;
    }
  }
  if (html.isEmpty() || runtime.highlightCss.isEmpty() ||
      !profiles->setProtectedDocument(runtime.profile.profile, html, resources)) {
    releaseProtectedSession();
    emit protectedSessionReady(nullptr, {});
    return;
  }
  runtime.view = new WebViewer(background, 1.0, nullptr, runtime.profile.profile);
  runtime.view->setAttribute(Qt::WA_DontShowOnScreen);
  runtime.view->setAttribute(Qt::WA_ShowWithoutActivating);
  runtime.view->setFocusPolicy(Qt::NoFocus);
  runtime.view->setContextMenuPolicy(Qt::NoContextMenu);
  runtime.view->resize(1024, 768);
  auto *page = qobject_cast<WebPage *>(runtime.view->page());
  page->setProtectedDocumentUrl(runtime.profile.url);
  page->setSensitiveContent(true);
  // A helper is not an interactive viewer and must never open desktop URLs.
  disconnect(page, &WebPage::externalLinkRequested, runtime.view, nullptr);
  page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
  page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
  page->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
  page->settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
  auto *channel = new QWebChannel(page);
  auto *adapter = new MarkdownViewerAdapter(m_services, page);
  adapter->setProtectedView(true);
  channel->registerObject(QStringLiteral("vxAdapter"), adapter);
  page->setWebChannel(channel);
  runtime.session = new WebViewJobSession(page, channel, page);
  runtime.session->setProperty("markdownJobProtected", true);
  runtime.session->setProperty("markdownJobBufferId", m_buffer.id());
  runtime.view->show();
  validateRuntimeOnLoad(page, runtime.session);
  page->load(runtime.profile.url);
  emit protectedSessionReady(runtime.session, runtime.highlightCss);
}

void MarkdownWebJobHost::releaseProtectedSession() {
  if (!m_protected) {
    return;
  }
  // Detach first: cancelling callbacks can re-enter host release via the controller.
  auto runtime = std::move(m_protected);
  m_services.get<WebEngineProfileService>()->revokeProtectedProfile(runtime->profile.profile);
  delete runtime->session;
  delete runtime->view;
  delete runtime->profile.profile;
}

void MarkdownWebJobHost::invalidate() { releaseProtectedSession(); }

void MarkdownWebJobHost::onProtectedLockingChanged(bool p_locking) {
  if (p_locking) {
    releaseProtectedSession();
  }
}
