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
#include "vfile.h"
#include "utils/vclipboardutils.h"
#include "utils/viconutils.h"

// We set the property of the clipboard to mark that the URL copied in the
// clipboard has been altered.
static const QString c_ClipboardPropertyMark = "CopiedImageURLAltered";

VWebView::VWebView(VFile *p_file, QWidget *p_parent)
    : QWebEngineView(p_parent),
      m_file(p_file),
      m_copyImageUrlActionHooked(false),
      m_copyActionHooked(false)
{
    setAcceptDrops(false);
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
        QAction *copyImageUrlAct = getPageAction(actions, QWebEnginePage::CopyImageUrlToClipboard);
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

    // We need to replace the "Copy Image" action, because the default one use
    // the fully-encoded URL to fetch the image while Windows seems to not
    // recognize it.
#if defined(Q_OS_WIN)
    QAction *defaultCopyImageAct = getPageAction(actions, QWebEnginePage::CopyImageToClipboard);
    if (defaultCopyImageAct && actions.contains(defaultCopyImageAct)) {
        QAction *copyImageAct = new QAction(defaultCopyImageAct->text(), menu);
        copyImageAct->setToolTip(defaultCopyImageAct->toolTip());
        connect(copyImageAct, &QAction::triggered,
                this, &VWebView::copyImage);
        menu->insertAction(defaultCopyImageAct, copyImageAct);
        defaultCopyImageAct->setVisible(false);
    }
#endif

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
            if (urls[0].isLocalFile()) {
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
                clipboard->setMimeData(data);

                clipboard->setProperty(c_ClipboardPropertyMark.toLatin1(), true);
                qDebug() << "clipboard copy image URL altered" << spaceOnlyText;
            }
        }
    }
}

void VWebView::hideUnusedActions(QMenu *p_menu)
{
    const QList<QAction *> actions = p_menu->actions();

    QList<QAction *> unusedActions;

    // Back.
    QAction *act = getPageAction(actions, QWebEnginePage::Back);
    unusedActions.append(act);

    // Forward.
    act = getPageAction(actions, QWebEnginePage::Forward);
    unusedActions.append(act);

    // Reload.
    act = getPageAction(actions, QWebEnginePage::Reload);
    unusedActions.append(act);

    // ViewSource.
    act = getPageAction(actions, QWebEnginePage::ViewSource);
    unusedActions.append(act);

    // DownloadImageToDisk.
    act = getPageAction(actions, QWebEnginePage::DownloadImageToDisk);
    unusedActions.append(act);

    for (auto it : unusedActions) {
        if (it) {
            it->setVisible(false);
        }
    }
}

void VWebView::handleCopyAction()
{
    // To avoid failure of setting clipboard mime data.
    QCoreApplication::processEvents();

    QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    clipboard->setProperty(c_ClipboardPropertyMark.toLatin1(), false);
    qDebug() << clipboard->ownsClipboard() << mimeData->hasHtml();
    if (clipboard->ownsClipboard()
        && mimeData->hasHtml()) {
        QString html = mimeData->html();
        if (html.startsWith("<html>")) {
            return;
        }

        html = QString("<html><body><!--StartFragment-->%1<!--EndFragment--></body></html>").arg(html);

        // Set new mime data.
        QMimeData *data = new QMimeData();
        data->setHtml(html);

        if (mimeData->hasUrls()) {
            data->setUrls(mimeData->urls());
        }

        if (mimeData->hasText()) {
            data->setText(mimeData->text());
        }

        clipboard->setMimeData(data);
        clipboard->setProperty(c_ClipboardPropertyMark.toLatin1(), true);
        qDebug() << "clipboard copy Html altered" << html;
    }
}

QAction *VWebView::getPageAction(const QList<QAction *> &p_actions,
                                 QWebEnginePage::WebAction p_webAction)
{
    QAction *act = pageAction(p_webAction);
    if (act && !p_actions.contains(act)) {
        QString text = act->text();
        for (auto it : p_actions) {
            if (it->text().endsWith(text)) {
                act = it;
                break;
            }
        }
    }

    return act;
}
