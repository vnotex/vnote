#include "markdownviewer.h"

#include <QWebChannel>
#include <QContextMenuEvent>
#include <QMenu>
#include <QApplication>
#include <QMimeData>
#include <QScopedPointer>

#include "markdownvieweradapter.h"
#include "previewhelper.h"
#include <utils/clipboardutils.h>
#include <utils/fileutils.h>
#include <utils/utils.h>
#include <utils/widgetutils.h>
#include <core/configmgr.h>
#include <core/editorconfig.h>
#include "../widgetsfactory.h"

using namespace vnotex;

// We set the property of the clipboard to mark that the URL copied in the
// clipboard has been altered.
static const char *c_propertyImageUrlAltered = "CopiedImageUrlAltered";

// Indicate whether this clipboard change is triggered by cross copy.
static const char *c_propertyCrossCopy = "CrossCopy";

MarkdownViewer::MarkdownViewer(MarkdownViewerAdapter *p_adapter,
                               const QColor &p_background,
                               qreal p_zoomFactor,
                               QWidget *p_parent)
    : WebViewer(p_background, p_zoomFactor, p_parent),
      m_adapter(p_adapter)
{
    m_adapter->setParent(this);

    auto channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("vxAdapter"), m_adapter);

    page()->setWebChannel(channel);

    connect(QApplication::clipboard(), &QClipboard::changed,
            this, &MarkdownViewer::handleClipboardChanged);

    connect(m_adapter, &MarkdownViewerAdapter::keyPressed,
            this, &MarkdownViewer::handleWebKeyPress);

    connect(m_adapter, &MarkdownViewerAdapter::zoomed,
            this, [this](bool p_zoomIn) {
                p_zoomIn ? zoomIn() : zoomOut();
            });

    connect(m_adapter, &MarkdownViewerAdapter::crossCopyReady,
            this, [](quint64 p_id, quint64 p_timeStamp, const QString &p_html) {
                Q_UNUSED(p_id);
                Q_UNUSED(p_timeStamp);
                std::unique_ptr<QMimeData> mimeData(new QMimeData());
                mimeData->setHtml(p_html);
                ClipboardUtils::setMimeDataToClipboard(QApplication::clipboard(), mimeData.release());
            });
}

MarkdownViewerAdapter *MarkdownViewer::adapter() const
{
    return m_adapter;
}

void MarkdownViewer::setPreviewHelper(PreviewHelper *p_previewHelper)
{
    connect(p_previewHelper, &PreviewHelper::graphPreviewRequested,
            this, [this, p_previewHelper](quint64 p_id,
                                          TimeStamp p_timeStamp,
                                          const QString &p_lang,
                                          const QString &p_text) {
                if (m_adapter->isViewerReady()) {
                    m_adapter->graphPreviewRequested(p_id, p_timeStamp, p_lang, p_text);
                } else {
                    p_previewHelper->handleGraphPreviewData(MarkdownViewerAdapter::PreviewData());
                }
            });
    connect(p_previewHelper, &PreviewHelper::mathPreviewRequested,
            this, [this, p_previewHelper](quint64 p_id,
                                          TimeStamp p_timeStamp,
                                          const QString &p_text) {
                if (m_adapter->isViewerReady()) {
                    m_adapter->mathPreviewRequested(p_id, p_timeStamp, p_text);
                } else {
                    p_previewHelper->handleMathPreviewData(MarkdownViewerAdapter::PreviewData());
                }
            });
    connect(m_adapter, &MarkdownViewerAdapter::graphPreviewDataReady,
            p_previewHelper, &PreviewHelper::handleGraphPreviewData);
    connect(m_adapter, &MarkdownViewerAdapter::mathPreviewDataReady,
            p_previewHelper, &PreviewHelper::handleMathPreviewData);
}

void MarkdownViewer::contextMenuEvent(QContextMenuEvent *p_event)
{
    QScopedPointer<QMenu> menu(page()->createStandardContextMenu());
    const QList<QAction *> actions = menu->actions();

#if defined(Q_OS_WIN)
    if (!m_copyImageUrlActionHooked) {
        // "Copy Image URL" action will put the encoded URL to the clipboard as text
        // and the URL as URLs. If the URL contains Chinese, OneNote or Word could not
        // recognize it.
        // We need to change it to only-space-encoded text.
        QAction *copyImageUrlAct = pageAction(QWebEnginePage::CopyImageUrlToClipboard);
        if (actions.contains(copyImageUrlAct)) {
            connect(copyImageUrlAct, &QAction::triggered,
                    this, &MarkdownViewer::handleCopyImageUrlAction);
            m_copyImageUrlActionHooked = true;
        }
    }
#endif

    if (!hasSelection()) {
        auto firstAct = actions.isEmpty() ? nullptr : actions[0];
        auto editAct = new QAction(tr("&Edit"), menu.data());
        WidgetUtils::addActionShortcutText(editAct,
                                           ConfigMgr::getInst().getEditorConfig().getShortcut(EditorConfig::Shortcut::EditRead));
        connect(editAct, &QAction::triggered,
                this, &MarkdownViewer::editRequested);
        menu->insertAction(firstAct, editAct);
        if (firstAct) {
            menu->insertSeparator(firstAct);
        }
    }

    // We need to replace the "Copy Image" action:
    // - the default one use the fully-encoded URL to fetch the image while
    // Windows seems to not recognize it.
    // - We need to remove the html to let it be recognized by some web pages.
    {
        auto defaultCopyImageAct = pageAction(QWebEnginePage::CopyImageToClipboard);
        if (actions.contains(defaultCopyImageAct)) {
            QAction *copyImageAct = new QAction(defaultCopyImageAct->text(), menu.data());
            copyImageAct->setToolTip(defaultCopyImageAct->toolTip());
            connect(copyImageAct, &QAction::triggered,
                    this, &MarkdownViewer::copyImage);
            menu->insertAction(defaultCopyImageAct, copyImageAct);
            defaultCopyImageAct->setVisible(false);
        }
    }

    {
        auto copyAct = pageAction(QWebEnginePage::Copy);
        if (actions.contains(copyAct)) {
            setupCrossCopyMenu(menu.data(), copyAct);
        }
    }

    hideUnusedActions(menu.data());

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
}

void MarkdownViewer::handleCopyImageUrlAction()
{
    // To avoid failure of setting clipboard mime data.
    QCoreApplication::processEvents();

    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    clipboard->setProperty(c_propertyImageUrlAltered, false);
    if (clipboard->ownsClipboard()
        && mimeData->hasText()
        && mimeData->hasUrls()) {
        QString text = mimeData->text();
        QList<QUrl> urls = mimeData->urls();
        if (urls.size() == 1
            && urls[0].isLocalFile()
            && urls[0].toEncoded() == text) {
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

void MarkdownViewer::copyImage()
{
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
            QImage img = FileUtils::imageFromFile(imgPath);
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

void MarkdownViewer::handleClipboardChanged(QClipboard::Mode p_mode)
{
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

void MarkdownViewer::removeHtmlFromImageData(QClipboard *p_clipboard,
                                             const QMimeData *p_mimeData)
{
    if (!p_mimeData->hasImage()) {
        return;
    }

    if (p_mimeData->hasHtml()) {
        qDebug() << "remove HTML from image QMimeData" << p_mimeData->html();
        QMimeData *data = new QMimeData();
        data->setImageData(p_mimeData->imageData());
        ClipboardUtils::setMimeDataToClipboard(p_clipboard, data, QClipboard::Clipboard);
    }
}

void MarkdownViewer::hideUnusedActions(QMenu *p_menu)
{
    QList<QAction *> unusedActions;

    // QWebEnginePage uses different actions of Back/Forward/Reload.
    // [Woboq](https://code.woboq.org/qt5/qtwebengine/src/webenginewidgets/api/qwebenginepage.cpp.html#1652)
    // We tell these three actions by name.
   const QStringList actionNames({QWebEnginePage::tr("&Back"),
                                  QWebEnginePage::tr("&Forward"),
                                  QWebEnginePage::tr("&Reload")});

    const QList<QAction *> actions = p_menu->actions();
    for (auto it : actions) {
        if (actionNames.contains(it->text())) {
            unusedActions.append(it);
        }
    }

    QVector<QWebEnginePage::WebAction> pageActions = { QWebEnginePage::SavePage,
                                                       QWebEnginePage::ViewSource,
                                                       QWebEnginePage::DownloadImageToDisk,
                                                       QWebEnginePage::DownloadLinkToDisk,
                                                       QWebEnginePage::OpenLinkInThisWindow,
                                                       QWebEnginePage::OpenLinkInNewBackgroundTab,
                                                       QWebEnginePage::OpenLinkInNewTab,
                                                       QWebEnginePage::OpenLinkInNewWindow
                                                     };

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

void MarkdownViewer::handleWebKeyPress(int p_key, bool p_ctrl, bool p_shift, bool p_meta)
{
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

void MarkdownViewer::zoomOut()
{
    qreal factor = zoomFactor();
    if (factor > 0.25) {
        factor -= 0.25;
        setZoomFactor(factor);
        emit zoomFactorChanged(factor);
    }
}

void MarkdownViewer::zoomIn()
{
    qreal factor = zoomFactor();
    factor += 0.25;
    setZoomFactor(factor);
    emit zoomFactorChanged(factor);
}

void MarkdownViewer::restoreZoom()
{
    setZoomFactor(1);
    emit zoomFactorChanged(1);
}

void MarkdownViewer::setupCrossCopyMenu(QMenu *p_menu, QAction *p_copyAct)
{
    const auto &targets = m_adapter->getCrossCopyTargets();
    if (targets.isEmpty()) {
        return;
    }

    auto subMenu = WidgetsFactory::createMenu(tr("Cross Copy"), p_menu);

    for (const auto &target : targets) {
        auto act = subMenu->addAction(m_adapter->getCrossCopyTargetDisplayName(target));
        act->setData(target);
    }

    connect(subMenu, &QMenu::triggered,
            this, [this](QAction *p_act) {
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

void MarkdownViewer::crossCopy(const QString &p_target, const QString &p_baseUrl, const QString &p_html)
{
    emit m_adapter->crossCopyRequested(0, 0, p_target, p_baseUrl, p_html);
}
