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
#include <QFileInfo>
#include "vfile.h"
#include "utils/vclipboardutils.h"
#include "utils/viconutils.h"
#include "vconfigmanager.h"
#include "utils/vwebutils.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

extern VWebUtils *g_webUtils;

// We set the property of the clipboard to mark that the URL copied in the
// clipboard has been altered.
static const QString c_ClipboardPropertyMark = "CopiedImageURLAltered";

VWebView::VWebView(VFile *p_file, QWidget *p_parent)
    : QWebEngineView(p_parent),
      m_file(p_file),
      m_copyImageUrlActionHooked(false),
      m_afterCopyImage(false),
      m_inPreview(false)
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
    QAction *firstAction = actions.isEmpty() ? NULL : actions[0];

    bool selection = hasSelection();

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

    if (!selection) {
        if (m_inPreview) {
            QAction *expandAct = new QAction(tr("Expand/Restore Preview Area"), menu);
            VUtils::fixTextWithCaptainShortcut(expandAct, "ExpandLivePreview");
            connect(expandAct, &QAction::triggered,
                    this, &VWebView::requestExpandRestorePreviewArea);
            menu->insertAction(firstAction, expandAct);

            initPreviewTunnelMenu(firstAction, menu);

            if (firstAction) {
                menu->insertSeparator(firstAction);
            }
        } else {
            if (m_file && m_file->isModifiable()) {
                QAction *editAct= new QAction(VIconUtils::menuIcon(":/resources/icons/edit_note.svg"),
                                              tr("&Edit"),
                                              menu);
                editAct->setToolTip(tr("Edit current note"));
                connect(editAct, &QAction::triggered,
                        this, &VWebView::handleEditAction);
                menu->insertAction(firstAction, editAct);
                if (firstAction) {
                    menu->insertSeparator(firstAction);
                }
            }

            QAction *savePageAct = new QAction(QWebEnginePage::tr("Save &Page"), menu);
            connect(savePageAct, &QAction::triggered,
                    this, &VWebView::requestSavePage);
            menu->addAction(savePageAct);

            // In preview mode, add the right-click upload menu.
            QMenu *uploadImageMenu = new QMenu(tr("&Upload Image To"), menu);

            // Upload the image to GitHub image hosting.
            QAction *uploadImageToGithub = new QAction(tr("&GitHub"), uploadImageMenu);
            connect(uploadImageToGithub, &QAction::triggered, this, &VWebView::requestUploadImageToGithub);
            uploadImageMenu->addAction(uploadImageToGithub);

            // Upload the image to Gitee image hosting.
            QAction *uploadImageToGitee = new QAction(tr("&Gitee"), uploadImageMenu);
            connect(uploadImageToGitee, &QAction::triggered, this, &VWebView::requestUploadImageToGitee);
            uploadImageMenu->addAction(uploadImageToGitee);

            // Upload the image to Wechat image hosting
            QAction *uploadImageToWechat = new QAction(tr("&Wechat"), uploadImageMenu);
            connect(uploadImageToWechat, &QAction::triggered, this, &VWebView::requestUploadImageToWechat);
            uploadImageMenu->addAction(uploadImageToWechat);

            // Upload the image to Tencent image hosting.
            QAction *uploadImageToTencent = new QAction(tr("&Tencent"), uploadImageMenu);
            connect(uploadImageToTencent, &QAction::triggered, this, &VWebView::requestUploadImageToTencent);
            uploadImageMenu->addAction(uploadImageToTencent);

            menu->addMenu(uploadImageMenu);
        }
    }

    // Add Copy As menu.
    {
        QAction *copyAct = pageAction(QWebEnginePage::Copy);
        if (actions.contains(copyAct) && !m_inPreview) {
            initCopyAsMenu(copyAct, menu);
        }
    }

    // We need to replace the "Copy Image" action:
    // - the default one use the fully-encoded URL to fetch the image while
    // Windows seems to not recognize it.
    // - We need to remove the html to let it be recognized by some web pages.
    QAction *defaultCopyImageAct = pageAction(QWebEnginePage::CopyImageToClipboard);
    if (actions.contains(defaultCopyImageAct)) {
        QAction *copyImageAct = new QAction(defaultCopyImageAct->text(), menu);
        copyImageAct->setToolTip(defaultCopyImageAct->toolTip());
        connect(copyImageAct, &QAction::triggered,
                this, &VWebView::copyImage);
        menu->insertAction(defaultCopyImageAct, copyImageAct);
        defaultCopyImageAct->setVisible(false);
    }

    // Add Copy All As menu.
    if (!m_inPreview) {
        initCopyAllAsMenu(menu);
    }

    hideUnusedActions(menu);

    p_event->accept();

    bool valid = false;
    for (auto act : menu->actions()) {
        if (act->isVisible()) {
            valid = true;
            break;
        }
    }

    if (valid) {
        menu->exec(p_event->globalPos());
    }

    delete menu;
}

void VWebView::handleEditAction()
{
    emit editNote();
}

void VWebView::copyImage()
{
#if defined(Q_OS_WIN)
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
            QImage img = VUtils::imageFromFile(imgPath);
            if (!img.isNull()) {
                m_afterCopyImage = false;
                VClipboardUtils::setImageToClipboard(clipboard, img, QClipboard::Clipboard);
                qDebug() << "clipboard copy image via URL" << imgPath;
                return;
            }
        }
    }
#endif

    m_afterCopyImage = true;

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

bool VWebView::removeStyles(QString &p_html)
{
    bool changed = false;

    const QStringList &styles = g_config->getStylesToRemoveWhenCopied();
    if (styles.isEmpty()) {
        return changed;
    }

    QRegExp tagReg("(<[^>]+\\sstyle=[^>]*>)");

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(tagReg, pos);
        if (idx == -1) {
            break;
        }

        QString styleStr = tagReg.cap(1);
        QString alteredStyleStr = styleStr;

        QString regPatt("(\\s|\")%1:[^;]+;");
        for (auto const & sty : styles) {
            QRegExp reg(regPatt.arg(sty));
            alteredStyleStr.replace(reg, "\\1");
        }

        pos = idx + tagReg.matchedLength();
        if (styleStr != alteredStyleStr) {
            pos = pos + alteredStyleStr.size() - styleStr.size();
            p_html.replace(idx, tagReg.matchedLength(), alteredStyleStr);
            changed = true;
        }
    }

    return changed;
}

void VWebView::handleClipboardChanged(QClipboard::Mode p_mode)
{
    if (!hasFocus()
        || p_mode != QClipboard::Clipboard) {
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard->ownsClipboard()) {
        return;
    }

    const QMimeData *mimeData = clipboard->mimeData();

    QString copyTarget = m_copyTarget;
    m_copyTarget.clear();

    if (m_afterCopyImage) {
        m_afterCopyImage = false;
        removeHtmlFromImageData(clipboard, mimeData);
    } else {
        alterHtmlMimeData(clipboard, mimeData, copyTarget);
    }
}

void VWebView::alterHtmlMimeData(QClipboard *p_clipboard,
                                 const QMimeData *p_mimeData,
                                 const QString &p_copyTarget)
{
    if (!p_mimeData->hasHtml()
        || p_mimeData->hasImage()
        || p_copyTarget.isEmpty()) {
        return;
    }

    QString html = p_mimeData->html();
    if (!g_webUtils->alterHtmlAsTarget(url(), html, p_copyTarget)) {
        return;
    }

    // Set new mime data.
    QMimeData *data = VClipboardUtils::cloneMimeData(p_mimeData);
    data->setHtml(html);

    VClipboardUtils::setMimeDataToClipboard(p_clipboard, data, QClipboard::Clipboard);
    qDebug() << "altered clipboard's Html";
}

void VWebView::removeHtmlFromImageData(QClipboard *p_clipboard,
                                       const QMimeData *p_mimeData)
{
    if (!p_mimeData->hasImage()) {
        return;
    }

    if (p_mimeData->hasHtml()) {
        qDebug() << "remove html from image data" << p_mimeData->html();
        QMimeData *data = new QMimeData();
        data->setImageData(p_mimeData->imageData());
        VClipboardUtils::setMimeDataToClipboard(p_clipboard, data, QClipboard::Clipboard);
    }
}

void VWebView::initCopyAsMenu(QAction *p_after, QMenu *p_menu)
{
    QStringList targets = g_webUtils->getCopyTargetsName();
    if (targets.isEmpty()) {
        return;
    }

    QMenu *subMenu = new QMenu(tr("Copy As"), p_menu);
    subMenu->setToolTipsVisible(true);
    for (auto const & target : targets) {
        QAction *act = new QAction(target, subMenu);
        act->setData(target);
        act->setToolTip(tr("Copy selected content using rules specified by target %1").arg(target));

        subMenu->addAction(act);
    }

    connect(subMenu, &QMenu::triggered,
            this, &VWebView::handleCopyAsAction);

    QAction *menuAct = p_menu->insertMenu(p_after, subMenu);
    p_menu->removeAction(p_after);
    p_menu->insertAction(menuAct, p_after);
}

void VWebView::handleCopyAsAction(QAction *p_act)
{
    if (!p_act) {
        return;
    }

    m_copyTarget = p_act->data().toString();

    triggerPageAction(QWebEnginePage::Copy);
}

void VWebView::initCopyAllAsMenu(QMenu *p_menu)
{
    QStringList targets = g_webUtils->getCopyTargetsName();
    if (targets.isEmpty()) {
        return;
    }

    QMenu *subMenu = new QMenu(tr("Copy All As"), p_menu);
    subMenu->setToolTipsVisible(true);
    for (auto const & target : targets) {
        QAction *act = new QAction(target, subMenu);
        act->setData(target);
        act->setToolTip(tr("Copy all content using rules specified by target %1").arg(target));

        subMenu->addAction(act);
    }

    connect(subMenu, &QMenu::triggered,
            this, &VWebView::handleCopyAllAsAction);

    p_menu->addSeparator();
    p_menu->addMenu(subMenu);
}

void VWebView::handleCopyAllAsAction(QAction *p_act)
{
    if (!p_act) {
        return;
    }

    triggerPageAction(QWebEnginePage::SelectAll);

    m_copyTarget = p_act->data().toString();

    triggerPageAction(QWebEnginePage::Copy);

    triggerPageAction(QWebEnginePage::Unselect);
}

void VWebView::initPreviewTunnelMenu(QAction *p_before, QMenu *p_menu)
{
    QMenu *subMenu = new QMenu(tr("Live Preview Tunnel"), p_menu);

    int config = g_config->getSmartLivePreview();

    QActionGroup *ag = new QActionGroup(subMenu);
    QAction *act = ag->addAction(tr("Disabled"));
    act->setData(SmartLivePreview::Disabled);
    act->setCheckable(true);
    if (act->data().toInt() == config) {
        act->setChecked(true);
    }

    act = ag->addAction(tr("Editor -> Preview"));
    act->setData(SmartLivePreview::EditorToWeb);
    act->setCheckable(true);
    if (act->data().toInt() == config) {
        act->setChecked(true);
    }

    act = ag->addAction(tr("Preview -> Editor"));
    act->setData(SmartLivePreview::WebToEditor);
    act->setCheckable(true);
    if (act->data().toInt() == config) {
        act->setChecked(true);
    }

    act = ag->addAction(tr("Bidirectional"));
    act->setData(SmartLivePreview::EditorToWeb | SmartLivePreview::WebToEditor);
    act->setCheckable(true);
    if (act->data().toInt() == config) {
        act->setChecked(true);
    }

    connect(ag, &QActionGroup::triggered,
            this, [](QAction *p_act) {
                int data = p_act->data().toInt();
                g_config->setSmartLivePreview(data);
            });

    subMenu->addActions(ag->actions());

    p_menu->insertMenu(p_before, subMenu);
}
