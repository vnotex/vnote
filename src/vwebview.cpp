#include "vwebview.h"

#include <QMenu>
#include <QPoint>
#include <QContextMenuEvent>
#include <QWebEnginePage>
#include <QAction>
#include <QList>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>
#include <QImage>
#include <QRegExp>
#include <QFileInfo>
#include "vfile.h"
#include "utils/vclipboardutils.h"
#include "utils/viconutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

// We set the property of the clipboard to mark that the URL copied in the
// clipboard has been altered.
static const QString c_ClipboardPropertyMark = "CopiedImageURLAltered";

VWebView::VWebView(VFile *p_file, QWidget *p_parent)
    : QWebEngineView(p_parent),
      m_file(p_file),
      m_copyImageUrlActionHooked(false),
      m_needRemoveBackground(false),
      m_fixImgSrc(g_config->getFixImageSrcInWebWhenCopied())
{
    setAcceptDrops(false);

    connect(QApplication::clipboard(), &QClipboard::changed,
            this, &VWebView::handleClipboardChanged);
}

void VWebView::contextMenuEvent(QContextMenuEvent *p_event)
{
    QMenu *menu = page()->createStandardContextMenu();
    menu->setToolTipsVisible(true);

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
                    this, &VWebView::handleCopyImageUrlAction);

            m_copyImageUrlActionHooked = true;

            qDebug() << "hooked CopyImageUrl action" << copyImageUrlAct;
        }
    }
#endif

    if (!hasSelection() && m_file && m_file->isModifiable()) {
        QAction *editAct= new QAction(VIconUtils::menuIcon(":/resources/icons/edit_note.svg"),
                                      tr("&Edit"), menu);
        editAct->setToolTip(tr("Edit current note"));
        connect(editAct, &QAction::triggered,
                this, &VWebView::handleEditAction);
        menu->insertAction(actions.isEmpty() ? NULL : actions[0], editAct);
        // actions does not contain editAction.
        if (!actions.isEmpty()) {
            menu->insertSeparator(actions[0]);
        }
    }

    // Add Copy without Background action.
    QAction *copyAct = pageAction(QWebEnginePage::Copy);
    if (actions.contains(copyAct)) {
        QAction *copyWithoutBgAct = new QAction(tr("Copy &without Background"), menu);
        copyWithoutBgAct->setToolTip(tr("Copy selected content without background styles"));
        connect(copyWithoutBgAct, &QAction::triggered,
                this, &VWebView::handleCopyWithoutBackgroundAction);
        menu->insertAction(copyAct, copyWithoutBgAct);
        menu->removeAction(copyAct);
        menu->insertAction(copyWithoutBgAct, copyAct);
    }

    // We need to replace the "Copy Image" action, because the default one use
    // the fully-encoded URL to fetch the image while Windows seems to not
    // recognize it.
#if defined(Q_OS_WIN)
    QAction *defaultCopyImageAct = pageAction(QWebEnginePage::CopyImageToClipboard);
    if (actions.contains(defaultCopyImageAct)) {
        QAction *copyImageAct = new QAction(defaultCopyImageAct->text(), menu);
        copyImageAct->setToolTip(defaultCopyImageAct->toolTip());
        connect(copyImageAct, &QAction::triggered,
                this, &VWebView::copyImage);
        menu->insertAction(defaultCopyImageAct, copyImageAct);
        defaultCopyImageAct->setVisible(false);
    }
#endif

    // Add Copy All without Background action.
    QAction *copyAllWithoutBgAct = new QAction(tr("Copy &All without Background"), menu);
    copyAllWithoutBgAct->setToolTip(tr("Copy all contents without background styles"));
    connect(copyAllWithoutBgAct, &QAction::triggered,
            this, &VWebView::handleCopyAllWithoutBackgroundAction);
    // Add it to the back.
    menu->addSeparator();
    menu->addAction(copyAllWithoutBgAct);

    hideUnusedActions(menu);

    menu->exec(p_event->globalPos());
    delete menu;
}

void VWebView::handleEditAction()
{
    emit editNote();
}

void VWebView::copyImage()
{
    Q_ASSERT(m_copyImageUrlActionHooked);

    // triggerPageAction(QWebEnginePage::CopyImageUrlToClipboard) will not really
    // trigger the corresponding action. It just do the stuff directly.
    QAction *copyImageUrlAct = pageAction(QWebEnginePage::CopyImageUrlToClipboard);
    copyImageUrlAct->trigger();

    QCoreApplication::processEvents();

    QClipboard *clipboard = QApplication::clipboard();
    if (clipboard->property(c_ClipboardPropertyMark.toLatin1()).toBool()) {
        const QMimeData *mimeData = clipboard->mimeData();
        QString imgPath;
        if (mimeData->hasUrls()) {
            QList<QUrl> urls = mimeData->urls();
            if (!urls.isEmpty() && urls[0].isLocalFile()) {
                imgPath = urls[0].toLocalFile();
            }
        }

        if (!imgPath.isEmpty()) {
            QImage img(imgPath);
            if (!img.isNull()) {
                VClipboardUtils::setImageToClipboard(clipboard, img, QClipboard::Clipboard);
                qDebug() << "clipboard copy image via URL" << imgPath;
                return;
            }
        }
    }

    // Fall back.
    triggerPageAction(QWebEnginePage::CopyImageToClipboard);
}

void VWebView::handleCopyImageUrlAction()
{
    // To avoid failure of setting clipboard mime data.
    QCoreApplication::processEvents();

    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    clipboard->setProperty(c_ClipboardPropertyMark.toLatin1(), false);
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
                VClipboardUtils::setMimeDataToClipboard(clipboard, data, QClipboard::Clipboard);

                clipboard->setProperty(c_ClipboardPropertyMark.toLatin1(), true);
                qDebug() << "clipboard copy image URL altered" << spaceOnlyText;
            }
        }
    }
}

void VWebView::hideUnusedActions(QMenu *p_menu)
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

    // ViewSource.
    QAction *act = pageAction(QWebEnginePage::ViewSource);
    unusedActions.append(act);

    // DownloadImageToDisk.
    act = pageAction(QWebEnginePage::DownloadImageToDisk);
    unusedActions.append(act);

    // DownloadLinkToDisk.
    act = pageAction(QWebEnginePage::DownloadLinkToDisk);
    unusedActions.append(act);

    for (auto it : unusedActions) {
        if (it) {
            it->setVisible(false);
        }
    }
}

static bool removeBackgroundColor(QString &p_html)
{
    QRegExp reg("(\\s|\")background(-color)?:[^;]+;");
    int size = p_html.size();
    p_html.replace(reg, "\\1");
    return p_html.size() != size;
}

void VWebView::handleCopyWithoutBackgroundAction()
{
    m_needRemoveBackground = true;

    triggerPageAction(QWebEnginePage::Copy);
}

void VWebView::handleCopyAllWithoutBackgroundAction()
{
    triggerPageAction(QWebEnginePage::SelectAll);

    m_needRemoveBackground = true;
    triggerPageAction(QWebEnginePage::Copy);

    triggerPageAction(QWebEnginePage::Unselect);
}

void VWebView::handleClipboardChanged(QClipboard::Mode p_mode)
{
    bool removeBackground = m_needRemoveBackground;
    m_needRemoveBackground = false;

    if (!hasFocus()
        || p_mode != QClipboard::Clipboard) {
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    if (!clipboard->ownsClipboard()) {
        return;
    }

    alterHtmlMimeData(clipboard, mimeData, removeBackground);
}

bool VWebView::fixImgSrc(QString &p_html)
{
    bool changed = false;

#if defined(Q_OS_WIN)
    QUrl::ComponentFormattingOption strOpt = QUrl::EncodeSpaces;
#else
    QUrl::ComponentFormattingOption strOpt = QUrl::FullyEncoded;
#endif

    QRegExp reg("(<img src=\")([^\"]+)\"");
    QUrl baseUrl(url());

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString urlStr = reg.cap(2);
        QUrl imgUrl(urlStr);

        QString fixedStr;
        if (imgUrl.isRelative()) {
            fixedStr = baseUrl.resolved(imgUrl).toString(strOpt);
        } else if (imgUrl.isLocalFile()) {
            fixedStr = imgUrl.toString(strOpt);
        } else if (imgUrl.scheme() != "https" && imgUrl.scheme() != "http") {
            QString tmp = imgUrl.toString();
            if (QFileInfo::exists(tmp)) {
                fixedStr = QUrl::fromLocalFile(tmp).toString(strOpt);
            }
        }

        pos = idx + reg.matchedLength();
        if (!fixedStr.isEmpty() && urlStr != fixedStr) {
            qDebug() << "fix img url" << urlStr << fixedStr;
            // Insert one more space to avoid fix the url twice.
            pos = pos + fixedStr.size() + 1 - urlStr.size();
            p_html.replace(idx,
                           reg.matchedLength(),
                           QString("<img  src=\"%1\"").arg(fixedStr));
            changed = true;
        }
    }

    return changed;
}

void VWebView::alterHtmlMimeData(QClipboard *p_clipboard,
                                 const QMimeData *p_mimeData,
                                 bool p_removeBackground)
{
    if (!p_mimeData->hasHtml()) {
        return;
    }

    bool altered = false;
    QString html = p_mimeData->html();

    // Add surrounded tags.
    if (!html.startsWith("<html>")) {
        altered = true;
        html = QString("<html><body>%1</body></html>").arg(html);
    }

    // Remove background color.
    if (p_removeBackground && removeBackgroundColor(html)) {
        altered = true;
    }

    // Fix local relative images.
    if (m_fixImgSrc && fixImgSrc(html)) {
        altered = true;
    }

    if (!altered) {
        return;
    }

    // Set new mime data.
    QMimeData *data = VClipboardUtils::cloneMimeData(p_mimeData);
    data->setHtml(html);

    VClipboardUtils::setMimeDataToClipboard(p_clipboard, data, QClipboard::Clipboard);
    qDebug() << "altered clipboard's Html";
}
