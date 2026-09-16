#include "markdownviewer.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QScopedPointer>
#include <QUrl>
#include <QUuid>
#include <QWebChannel>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#include <QWebEngineContextMenuData>
#else
#include <QWebEngineContextMenuRequest>
#endif

#include <cmath>
#include <limits>
#include <utility>

#include <controllers/markdownviewwindowcontroller.h>

#include "../viewwindow2.h"
#include "../webpage.h"
#include "../widgetsfactory.h"
#include "markdownvieweradapter.h"
#include "previewhelper.h"
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/logging.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <gui/services/webengineprofileservice.h>
#include <gui/utils/imageutils.h>
#include <utils/clipboardutils.h>
#include <utils/utils.h>
#include <utils/widgetutils.h>
#include <widgets/messageboxhelper.h>

using namespace vnotex;

// We set the property of the clipboard to mark that the URL copied in the
// clipboard has been altered.
static const char *c_propertyImageUrlAltered = "CopiedImageUrlAltered";

// Indicate whether this clipboard change is triggered by cross copy.
static const char *c_propertyCrossCopy = "CrossCopy";

namespace {
// Resolve the shared web engine profile, if the service is registered.
QWebEngineProfile *resolveProfile(ServiceLocator &p_services, const ViewWindow2 *p_window,
                                  QWebEngineProfile *p_protectedProfile) {
  if (p_window && p_window->getBuffer().isEncrypted()) {
    if (!p_protectedProfile) {
      qFatal("A protected Markdown viewer requires its isolated profile");
    }
    return p_protectedProfile;
  }
  auto *svc = p_services.get<WebEngineProfileService>();
  return svc ? svc->profile() : nullptr;
}
} // namespace

MarkdownViewer::MarkdownViewer(MarkdownViewerAdapter *p_adapter, ServiceLocator &p_services,
                               const QColor &p_background, qreal p_zoomFactor, QWidget *p_parent)
    : MarkdownViewer(p_adapter, static_cast<const ViewWindow2 *>(nullptr), p_services, p_background,
                     p_zoomFactor, p_parent) {}

MarkdownViewer::MarkdownViewer(MarkdownViewerAdapter *p_adapter, const ViewWindow2 *p_viewWindow2,
                               ServiceLocator &p_services, const QColor &p_background,
                               qreal p_zoomFactor, QWidget *p_parent, QWebEngineProfile *p_profile)
    : WebViewer(p_background, p_zoomFactor, p_parent,
                resolveProfile(p_services, p_viewWindow2, p_profile)),
      m_protectedView(p_viewWindow2 && p_viewWindow2->getBuffer().isEncrypted()),
      m_adapter(p_adapter), m_viewWindow2(p_viewWindow2), m_services(p_services) {

  if (lcPerfPreview().isDebugEnabled()) {
    qCDebug(lcPerfPreview) << "[math-trace] phase=viewer-constructed"
                           << "atMs=" << QDateTime::currentMSecsSinceEpoch()
                           << "adapter=" << static_cast<const void *>(m_adapter)
                           << "viewer=" << static_cast<const void *>(this)
                           << "visible=" << isVisible() << "ready=" << m_adapter->isReady();
    QWebEngineScript traceScript;
    traceScript.setName(QStringLiteral("vx-math-trace"));
    traceScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    traceScript.setWorldId(QWebEngineScript::MainWorld);
    traceScript.setRunsOnSubFrames(false);
    traceScript.setSourceCode(QStringLiteral("window.vxMathTraceId = '0x%1';")
                                  .arg(reinterpret_cast<quintptr>(m_adapter), 0, 16));
    page()->scripts().insert(traceScript);
    connect(page(), &QWebEnginePage::loadStarted, this, [this]() {
      qCDebug(lcPerfPreview) << "[math-trace] phase=viewer-load-started"
                             << "atMs=" << QDateTime::currentMSecsSinceEpoch()
                             << "adapter=" << static_cast<const void *>(m_adapter)
                             << "visible=" << isVisible() << "ready=" << m_adapter->isReady();
    });
    connect(m_adapter, &MarkdownViewerAdapter::ready, this, [this]() {
      qCDebug(lcPerfPreview) << "[math-trace] phase=adapter-ready"
                             << "atMs=" << QDateTime::currentMSecsSinceEpoch()
                             << "adapter=" << static_cast<const void *>(m_adapter)
                             << "visible=" << isVisible() << "ready=" << m_adapter->isReady();
    });
  }

  m_adapter->setParent(this);
  if (m_protectedView) {
    m_adapter->setProtectedView(true);
  }

  auto channel = new QWebChannel(this);
  channel->registerObject(QStringLiteral("vxAdapter"), m_adapter);

  page()->setWebChannel(channel);

  connect(page(), &QWebEnginePage::loadStarted, this, &MarkdownViewer::invalidateNavigationTargets);
  connect(m_adapter, &MarkdownViewerAdapter::textUpdated, this,
          &MarkdownViewer::invalidateNavigationTargets);

  connect(QApplication::clipboard(), &QClipboard::changed, this,
          &MarkdownViewer::handleClipboardChanged);

  connect(m_adapter, &MarkdownViewerAdapter::keyPressed, this, &MarkdownViewer::handleWebKeyPress);

  connect(m_adapter, &MarkdownViewerAdapter::zoomed, this,
          [this](bool p_zoomIn) { p_zoomIn ? zoomIn() : zoomOut(); });

  connect(m_adapter, &MarkdownViewerAdapter::crossCopyReady, this,
          [](quint64 p_id, quint64 p_timeStamp, const QString &p_html) {
            Q_UNUSED(p_id);
            Q_UNUSED(p_timeStamp);
            std::unique_ptr<QMimeData> mimeData(new QMimeData());
            mimeData->setHtml(p_html);
            ClipboardUtils::setMimeDataToClipboard(QApplication::clipboard(), mimeData.release());
          });

  connect(this, &WebViewer::localFileOpenRequested, this, [](const QUrl &p_url) {
    Q_UNUSED(p_url);
    // File open handling is done by the owning MarkdownViewWindow2.
  });

  settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, !m_protectedView);
  if (m_protectedView) {
    settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings()->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
  }
}

MarkdownViewerAdapter *MarkdownViewer::adapter() const { return m_adapter; }

void MarkdownViewer::setController(MarkdownViewWindowController *p_controller) {
  m_controller = p_controller;
}

MarkdownViewerContextInfo MarkdownViewer::populateContextInfo() const {
  MarkdownViewerContextInfo info;
  info.hasSelection = hasSelection();
  info.inReadMode = m_viewWindow2 && m_viewWindow2->getMode() == ViewWindowMode::Read;
  const auto &targets = m_adapter->getCrossCopyTargets();
  for (const auto &t : targets) {
    info.crossCopyTargets.append(t);
    info.crossCopyDisplayNames.append(m_adapter->getCrossCopyTargetDisplayName(t));
  }
  info.editShortcutText = getEditorConfig().getShortcut(EditorConfig::Shortcut::EditRead);
  info.exportShortcutText =
      m_services.get<ConfigMgr2>()->getCoreConfig().getShortcut(CoreConfig::Shortcut::Export);
  info.copyAction = pageAction(QWebEnginePage::Copy);
  info.defaultCopyImageAction = pageAction(QWebEnginePage::CopyImageToClipboard);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
  const auto data = page()->contextMenuData();
  if (data.mediaType() == QWebEngineContextMenuData::MediaTypeImage) {
    info.imageUrl = data.mediaUrl();
  }
#else
  if (auto *req = lastContextMenuRequest()) {
    if (req->mediaType() == QWebEngineContextMenuRequest::MediaTypeImage) {
      info.imageUrl = req->mediaUrl();
    }
  }
#endif
  return info;
}

void MarkdownViewer::setPreviewHelper(PreviewHelper *p_previewHelper) {
  qCDebug(lcPerfPreview) << "[math-trace] phase=preview-helper-mapped"
                         << "atMs=" << QDateTime::currentMSecsSinceEpoch()
                         << "adapter=" << static_cast<const void *>(m_adapter)
                         << "helper=" << static_cast<const void *>(p_previewHelper);
  connect(p_previewHelper, &PreviewHelper::graphPreviewRequested, this,
          [this, p_previewHelper](quint64 p_id, TimeStamp p_timeStamp, const QString &p_lang,
                                  const QString &p_text, qreal p_scale) {
            if (m_adapter->isReady()) {
              m_adapter->graphPreviewRequested(p_id, p_timeStamp, p_lang, p_text, p_scale);
            } else {
              p_previewHelper->handleGraphPreviewData(MarkdownViewerAdapter::PreviewData());
            }
          });
  connect(p_previewHelper, &PreviewHelper::mathPreviewRequested, this,
          [this, p_previewHelper](quint64 p_id, TimeStamp p_timeStamp, const QString &p_text,
                                  qreal p_scale) {
            const bool ready = m_adapter->isReady();
            qCDebug(lcPerfPreview)
                << "[math-trace] phase="
                << (ready ? "request-forwarded" : "request-dropped-not-ready")
                << "atMs=" << QDateTime::currentMSecsSinceEpoch()
                << "adapter=" << static_cast<const void *>(m_adapter)
                << "helper=" << static_cast<const void *>(p_previewHelper) << "ts=" << p_timeStamp
                << "id=" << p_id << "chars=" << p_text.size() << "scale=" << p_scale
                << "visible=" << isVisible();
            if (ready) {
              m_adapter->mathPreviewRequested(p_id, p_timeStamp, p_text, p_scale);
            } else {
              p_previewHelper->handleMathPreviewData(MarkdownViewerAdapter::PreviewData());
            }
          });
  connect(m_adapter, &MarkdownViewerAdapter::graphPreviewDataReady, p_previewHelper,
          &PreviewHelper::handleGraphPreviewData);
  connect(m_adapter, &MarkdownViewerAdapter::mathPreviewDataReady, p_previewHelper,
          &PreviewHelper::handleMathPreviewData);
}

void MarkdownViewer::invalidateNavigationTargets() { ++m_navigationGeneration; }

void MarkdownViewer::fetchNavigationTargets(NavigationTargetsCallback p_callback) {
  invalidateNavigationTargets();
  if (!p_callback) {
    return;
  }
  const QPointer<MarkdownViewer> viewer(this);
  const QPointer<QWebEnginePage> viewerPage(page());
  const auto generation = m_navigationGeneration;
  const auto viewerSize = size();
  const auto zoom = zoomFactor();
  const auto current = [viewer, viewerPage, generation, viewerSize, zoom]() {
    return viewer && viewerPage && viewer->m_navigationGeneration == generation &&
           viewer->page() == viewerPage.data() && viewer->isVisible() &&
           viewer->m_adapter->isReady() && viewer->size() == viewerSize && !viewerSize.isEmpty() &&
           std::isfinite(zoom) && zoom > 0 && viewer->zoomFactor() == zoom;
  };
  if (!current()) {
    p_callback({});
    return;
  }
  viewerPage->runJavaScript(
      QStringLiteral("window.vxcore && typeof window.vxcore.getNavigationTargets === 'function'"
                     " ? window.vxcore.getNavigationTargets() : null"),
      [viewer, current, zoom, p_callback = std::move(p_callback)](const QVariant &p_result) {
        if (!current()) {
          p_callback({});
          return;
        }
        const auto result = QJsonValue::fromVariant(p_result);
        const auto object = result.toObject();
        const auto snapshotValue = object.value(QStringLiteral("snapshot"));
        const auto snapshotNumber = snapshotValue.toDouble();
        // JavaScript's exact integer range, not the larger quint64 range.
        if (!result.isObject() || !snapshotValue.isDouble() || !std::isfinite(snapshotNumber) ||
            snapshotNumber <= 0 || snapshotNumber > 9007199254740991.0 ||
            std::floor(snapshotNumber) != snapshotNumber ||
            !object.value(QStringLiteral("targets")).isArray()) {
          p_callback({});
          return;
        }
        const auto snapshot = static_cast<quint64>(snapshotNumber);
        const auto entries = object.value(QStringLiteral("targets")).toArray();
        QVector<NavigationTarget> targets;
        targets.reserve(entries.size());
        for (const auto &entry : entries) {
          const auto target = entry.toObject();
          bool valid = entry.isObject();
          for (const auto *key : {"index", "x", "y", "width", "height"}) {
            const auto value = target.value(QLatin1String(key));
            valid = valid && value.isDouble() && std::isfinite(value.toDouble());
          }
          if (!valid) {
            continue;
          }
          const auto indexNumber = target.value(QStringLiteral("index")).toDouble();
          const auto x = target.value(QStringLiteral("x")).toDouble() * zoom;
          const auto y = target.value(QStringLiteral("y")).toDouble() * zoom;
          const auto width = target.value(QStringLiteral("width")).toDouble() * zoom;
          const auto height = target.value(QStringLiteral("height")).toDouble() * zoom;
          if (indexNumber < 0 || indexNumber > std::numeric_limits<int>::max() ||
              std::floor(indexNumber) != indexNumber || width <= 0 || height <= 0 ||
              !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
              !std::isfinite(height) || !std::isfinite(x + width) || !std::isfinite(y + height)) {
            continue;
          }
          // Clip in floating point before integer conversion to avoid overflow on invalid input.
          const auto rect = QRectF(x, y, width, height)
                                .intersected(QRectF(viewer->rect()))
                                .toAlignedRect()
                                .intersected(viewer->rect());
          if (rect.isEmpty()) {
            continue;
          }
          const auto index = static_cast<int>(indexNumber);
          targets.append({viewer.data(), rect, [viewer, current, snapshot, index]() {
                            if (current()) {
                              viewer->activateNavigationTarget(snapshot, index);
                            }
                          }});
        }
        p_callback(std::move(targets));
      });
}

void MarkdownViewer::activateNavigationTarget(quint64 p_snapshot, int p_index) {
  const QPointer<MarkdownViewer> viewer(this);
  const QPointer<QWebEnginePage> viewerPage(page());
  const auto generation = m_navigationGeneration;
  const auto viewerSize = size();
  const auto zoom = zoomFactor();
  const auto current = [viewer, viewerPage, generation, viewerSize, zoom]() {
    return viewer && viewerPage && viewer->m_navigationGeneration == generation &&
           viewer->page() == viewerPage.data() && viewer->isVisible() &&
           viewer->m_adapter->isReady() && viewer->size() == viewerSize && !viewerSize.isEmpty() &&
           std::isfinite(zoom) && zoom > 0 && viewer->zoomFactor() == zoom;
  };
  if (!current()) {
    return;
  }
  viewerPage->runJavaScript(
      QStringLiteral("window.vxcore && typeof window.vxcore.resolveNavigationTarget === 'function'"
                     " ? window.vxcore.resolveNavigationTarget(%1, %2) : null")
          .arg(p_snapshot)
          .arg(p_index),
      [viewer, current](const QVariant &p_result) {
        // The mode/page may change while the resolver is in flight, after the outer callback.
        if (!current()) {
          return;
        }
        const auto result = QJsonValue::fromVariant(p_result);
        const auto object = result.toObject();
        const auto hrefValue = object.value(QStringLiteral("href"));
        const auto urlValue = object.value(QStringLiteral("url"));
        if (!result.isObject() || !hrefValue.isString() || !urlValue.isString()) {
          return;
        }
        const auto href = hrefValue.toString();
        const QUrl url(urlValue.toString());
        if (href.isEmpty() || url.isEmpty() || !url.isValid()) {
          return;
        }
        if (href.startsWith(QLatin1Char('#'))) {
          viewer->adapter()->scrollToPosition(
              MarkdownViewerAdapter::Position(-1, QUrl(href).fragment(QUrl::FullyDecoded)));
        } else if (viewer->m_protectedView && QUrl(href).isRelative()) {
          viewer->adapter()->activateProtectedLink(href);
        } else if (!viewer->m_protectedView && url.isLocalFile()) {
          emit viewer->localFileOpenRequested(url);
        } else if (!viewer->m_protectedView || url.scheme() == QStringLiteral("http") ||
                   url.scheme() == QStringLiteral("https")) {
          emit viewer->externalLinkRequested(url);
        }
      });
}

void MarkdownViewer::contextMenuEvent(QContextMenuEvent *p_event) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
  QMenu *menu(page()->createStandardContextMenu());
#else
  QMenu *menu(createStandardContextMenu());
#endif

  const QList<QAction *> actions = menu->actions();

#if defined(Q_OS_WIN)
  if (!m_copyImageUrlActionHooked) {
    // "Copy Image URL" action will put the encoded URL to the clipboard as text
    // and the URL as URLs. If the URL contains Chinese, OneNote or Word could not
    // recognize it.
    // We need to change it to only-space-encoded text.
    QAction *copyImageUrlAct = pageAction(QWebEnginePage::CopyImageUrlToClipboard);
    if (actions.contains(copyImageUrlAct)) {
      connect(copyImageUrlAct, &QAction::triggered, this,
              &MarkdownViewer::handleCopyImageUrlAction);
      m_copyImageUrlActionHooked = true;
    }
  }
#endif

  if (m_controller) {
    auto info = populateContextInfo();
    const QUrl imageUrl = info.imageUrl;
    if (m_protectedView) {
      // Embedded data stays copyable; do not hand data/scoped URLs to external apps.
      info.imageUrl = QUrl();
    }
    m_controller->createContextMenu(
        info, menu, [this]() { copyImage(); }, [this]() { emit editRequested(); },
        [this](const QString &target) {
          m_crossCopyTarget = target;
          auto *clipboard = QApplication::clipboard();
          clipboard->setProperty(c_propertyCrossCopy, true);
          triggerPageAction(QWebEnginePage::Copy);
        },
        [this, imageUrl]() { openImageExternally(imageUrl); },
        [this]() { emit exportRequested(); });
  } else {
    // Fallback: inline logic for when no controller is set (e.g., WebViewExporter).
    // This preserves the exact original behavior.
    if (!hasSelection()) {
      bool inReadMode = false;
      if (m_viewWindow2) {
        inReadMode = m_viewWindow2->getMode() == ViewWindowMode::Read;
      }
      if (inReadMode) {
        auto firstAct = actions.isEmpty() ? nullptr : actions[0];
        auto editAct = new QAction(tr("&Edit"), menu);
        WidgetUtils::addActionShortcutText(
            editAct, getEditorConfig().getShortcut(EditorConfig::Shortcut::EditRead));
        connect(editAct, &QAction::triggered, this, &MarkdownViewer::editRequested);
        menu->insertAction(firstAct, editAct);
        if (firstAct) {
          menu->insertSeparator(firstAct);
        }
      }
    }

    // We need to replace the "Copy Image" action:
    // - the default one use the fully-encoded URL to fetch the image while
    // Windows seems to not recognize it.
    // - We need to remove the html to let it be recognized by some web pages.
    {
      auto defaultCopyImageAct = pageAction(QWebEnginePage::CopyImageToClipboard);
      if (actions.contains(defaultCopyImageAct)) {
        QAction *copyImageAct = new QAction(tr("Copy"), menu);
        copyImageAct->setToolTip(defaultCopyImageAct->toolTip());
        connect(copyImageAct, &QAction::triggered, this, &MarkdownViewer::copyImage);
        menu->insertAction(defaultCopyImageAct, copyImageAct);
        defaultCopyImageAct->setVisible(false);
      }
    }

    {
      auto copyAct = pageAction(QWebEnginePage::Copy);
      if (actions.contains(copyAct)) {
        setupCrossCopyMenu(menu, copyAct);
      }
    }
  }

  // Override "Copy image address" text from Qt's default to title case.
  {
    auto *copyImageUrlAct = pageAction(QWebEnginePage::CopyImageUrlToClipboard);
    if (actions.contains(copyImageUrlAct)) {
      copyImageUrlAct->setText(tr("Copy Image Address"));
    }
  }

  hideUnusedActions(menu);

  p_event->accept();

  bool valid = false;
  for (auto act : menu->actions()) {
    // There may be one action visible with text being empty.
    if (act->isVisible() && !act->text().isEmpty()) {
      valid = true;
      break;
    }
  }

  if (valid) {
    menu->exec(p_event->globalPos());
  }

  // For Qt 6, the menu is set with WA_DeleteOnClose.
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
  delete menu;
#endif
}

void MarkdownViewer::handleCopyImageUrlAction() {
  // To avoid failure of setting clipboard mime data.
  QCoreApplication::processEvents();

  QClipboard *clipboard = QApplication::clipboard();
  const QMimeData *mimeData = clipboard->mimeData();
  clipboard->setProperty(c_propertyImageUrlAltered, false);
  if (clipboard->ownsClipboard() && mimeData->hasText() && mimeData->hasUrls()) {
    QString text = mimeData->text();
    QList<QUrl> urls = mimeData->urls();
    if (urls.size() == 1 && urls[0].isLocalFile() && urls[0].toEncoded() == text) {
      QString spaceOnlyText = urls[0].toString(QUrl::EncodeSpaces);
      if (spaceOnlyText != text) {
        // Set new mime data.
        QMimeData *data = new QMimeData();
        data->setUrls(urls);
        data->setText(spaceOnlyText);
        ClipboardUtils::setMimeDataToClipboard(clipboard, data, QClipboard::Clipboard);
        clipboard->setProperty(c_propertyImageUrlAltered, true);
        qDebug() << "clipboard copy image URL altered" << spaceOnlyText;
      }
    }
  }
}

void MarkdownViewer::openImageExternally(const QUrl &p_url) {
  if (m_protectedView) {
    return;
  }
  if (!p_url.isValid()) {
    return;
  }
  const auto scheme = p_url.scheme();
  if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
    int ret = MessageBoxHelper::questionYesNo(
        MessageBoxHelper::Warning, tr("Are you sure to open link (%1)?").arg(p_url.toString()),
        tr("Malicious link might do harm to your device."), QString(), this);
    if (ret == QMessageBox::Yes) {
      QDesktopServices::openUrl(p_url);
    }
    return;
  }

  // Local file (or file:// URL): open with the default application.
  if (p_url.isLocalFile()) {
    WidgetUtils::openUrlByDesktop(p_url);
  }
  // Other schemes (data:/blob:/ftp:) are unsupported for external viewing; ignore.
}

void MarkdownViewer::copyImage() {
  if (m_protectedView) {
    m_copyImageTriggered = true;
    triggerPageAction(QWebEnginePage::CopyImageToClipboard);
    return;
  }
#if defined(Q_OS_WIN)
  Q_ASSERT(m_copyImageUrlActionHooked);
  // triggerPageAction(QWebEnginePage::CopyImageUrlToClipboard) will not really
  // trigger the corresponding action. It just do the stuff directly.
  QAction *copyImageUrlAct = pageAction(QWebEnginePage::CopyImageUrlToClipboard);
  copyImageUrlAct->trigger();

  QCoreApplication::processEvents();

  QClipboard *clipboard = QApplication::clipboard();
  if (clipboard->property(c_propertyImageUrlAltered).toBool()) {
    const QMimeData *mimeData = clipboard->mimeData();
    QString imgPath;
    if (mimeData->hasUrls()) {
      QList<QUrl> urls = mimeData->urls();
      if (!urls.isEmpty() && urls[0].isLocalFile()) {
        imgPath = urls[0].toLocalFile();
      }
    }

    if (!imgPath.isEmpty()) {
      QImage img = ImageUtils::imageFromFile(imgPath);
      if (!img.isNull()) {
        m_copyImageTriggered = false;
        ClipboardUtils::setImageToClipboard(clipboard, img, QClipboard::Clipboard);
        return;
      }
    }
  }
#endif

  m_copyImageTriggered = true;

  // Fall back.
  triggerPageAction(QWebEnginePage::CopyImageToClipboard);
}

void MarkdownViewer::handleClipboardChanged(QClipboard::Mode p_mode) {
  if (!hasFocus() || p_mode != QClipboard::Clipboard) {
    return;
  }

  QClipboard *clipboard = QApplication::clipboard();
  if (!clipboard->ownsClipboard()) {
    return;
  }

  const QMimeData *mimeData = clipboard->mimeData();
  if (m_copyImageTriggered) {
    m_copyImageTriggered = false;
    removeHtmlFromImageData(clipboard, mimeData);
    return;
  }

  if (clipboard->property(c_propertyCrossCopy).toBool()) {
    clipboard->setProperty(c_propertyCrossCopy, false);
    if (mimeData->hasHtml() && !mimeData->hasImage() && !m_crossCopyTarget.isEmpty()) {
      crossCopy(m_crossCopyTarget, url().toString(), mimeData->html());
    }
  }
}

void MarkdownViewer::removeHtmlFromImageData(QClipboard *p_clipboard, const QMimeData *p_mimeData) {
  if (!p_mimeData->hasImage()) {
    return;
  }

  if (p_mimeData->hasHtml()) {
    if (!m_protectedView) {
      qDebug() << "remove HTML from image QMimeData" << p_mimeData->html();
    }
    QMimeData *data = new QMimeData();
    data->setImageData(p_mimeData->imageData());
    ClipboardUtils::setMimeDataToClipboard(p_clipboard, data, QClipboard::Clipboard);
  }
}

void MarkdownViewer::hideUnusedActions(QMenu *p_menu) {
  Q_UNUSED(p_menu);

  QList<QAction *> unusedActions;

  // QWebEnginePage uses different actions of Back/Forward/Reload before Qt 5.15.
  // [Woboq](https://code.woboq.org/qt5/qtwebengine/src/webenginewidgets/api/qwebenginepage.cpp.html#1652)
  // We tell these three actions by name.
#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
  const QStringList actionNames(
      {QWebEnginePage::tr("&Back"), QWebEnginePage::tr("&Forward"), QWebEnginePage::tr("&Reload")});

  const QList<QAction *> actions = p_menu->actions();
  for (auto it : actions) {
    if (actionNames.contains(it->text())) {
      unusedActions.append(it);
    }
  }
#endif

  QVector<QWebEnginePage::WebAction> pageActions = {QWebEnginePage::SavePage,
                                                    QWebEnginePage::ViewSource,
                                                    QWebEnginePage::DownloadImageToDisk,
                                                    QWebEnginePage::DownloadLinkToDisk,
                                                    QWebEnginePage::OpenLinkInThisWindow,
                                                    QWebEnginePage::OpenLinkInNewBackgroundTab,
                                                    QWebEnginePage::OpenLinkInNewTab,
                                                    QWebEnginePage::OpenLinkInNewWindow,
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
                                                    QWebEnginePage::Forward,
                                                    QWebEnginePage::Back,
                                                    QWebEnginePage::Reload
#endif
  };

  if (m_protectedView) {
    pageActions.append(QWebEnginePage::CopyImageUrlToClipboard);
    pageActions.append(QWebEnginePage::InspectElement);
  }
  for (auto pageAct : pageActions) {
    auto act = pageAction(pageAct);
    unusedActions.append(act);
  }

  for (auto it : unusedActions) {
    if (it) {
      it->setVisible(false);
    }
  }
}

void MarkdownViewer::handleWebKeyPress(int p_key, bool p_ctrl, bool p_shift, bool p_meta) {
  Q_UNUSED(p_shift);
  Q_UNUSED(p_meta);
  switch (p_key) {
  // Esc
  case 27:
    break;

  // Dash
  case 189:
    if (p_ctrl) {
      // Zoom out.
      zoomOut();
    }
    break;

  // Equal
  case 187:
    if (p_ctrl) {
      // Zoom in.
      zoomIn();
    }
    break;

  // 0
  case 48:
    if (p_ctrl) {
      // Recover zoom.
      restoreZoom();
    }
    break;

  default:
    break;
  }
}

void MarkdownViewer::zoomOut() {
  qreal factor = zoomFactor();
  if (factor > 0.1) {
    factor -= 0.1;
    setZoomFactor(factor);
    emit zoomFactorChanged(factor);
  }
}

void MarkdownViewer::zoomIn() {
  qreal factor = zoomFactor();
  factor += 0.1;
  setZoomFactor(factor);
  emit zoomFactorChanged(factor);
}

void MarkdownViewer::restoreZoom() {
  setZoomFactor(1);
  emit zoomFactorChanged(1);
}

void MarkdownViewer::setupCrossCopyMenu(QMenu *p_menu, QAction *p_copyAct) {
  const auto &targets = m_adapter->getCrossCopyTargets();
  if (targets.isEmpty()) {
    return;
  }

  auto subMenu = WidgetsFactory::createMenu(tr("Cross Copy"), p_menu);

  for (const auto &target : targets) {
    auto act = subMenu->addAction(m_adapter->getCrossCopyTargetDisplayName(target));
    act->setData(target);
  }

  connect(subMenu, &QMenu::triggered, this, [this](QAction *p_act) {
    // selectedText() will return a plain text, so we trigger the Copy action here.
    m_crossCopyTarget = p_act->data().toString();

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setProperty(c_propertyCrossCopy, true);
    // Will handle the remaining logics in handleClipboardChanged().
    triggerPageAction(QWebEnginePage::Copy);
  });

  auto menuAct = p_menu->insertMenu(p_copyAct, subMenu);
  p_menu->removeAction(p_copyAct);
  p_menu->insertAction(menuAct, p_copyAct);
}

void MarkdownViewer::crossCopy(const QString &p_target, const QString &p_baseUrl,
                               const QString &p_html) {
  emit m_adapter->crossCopyRequested(0, 0, p_target, p_baseUrl, p_html);
}

void MarkdownViewer::saveContent(const std::function<void(const QString &p_content)> &p_callback) {
  page()->runJavaScript("document.getElementById('vx-content').textContent",
                        [p_callback](const QVariant &v) { p_callback(v.toString()); });
}

EditorConfig &MarkdownViewer::getEditorConfig() const {
  return m_services.get<ConfigMgr2>()->getEditorConfig();
}
