#include "urldragdroputils.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>

#include <utils/pathutils.h>

using namespace vnotex;

bool UrlDragDropUtils::handleDragEnterEvent(QDragEnterEvent *p_event)
{
    if (p_event->mimeData()->hasFormat(QStringLiteral("text/uri-list"))) {
        p_event->acceptProposedAction();
        return true;
    }

    return false;
}

bool UrlDragDropUtils::handleDropEvent(QDropEvent *p_event, std::function<void(const QStringList &)> p_func)
{
    const QMimeData *mime = p_event->mimeData();
    if (mime->hasFormat(QStringLiteral("text/uri-list")) && mime->hasUrls()) {
        QStringList files;
        const auto urls = mime->urls();
        for (const auto &url : urls) {
            if (url.isLocalFile()) {
                QFileInfo info(url.toLocalFile());
                if (info.exists() && info.isFile()) {
                    files << PathUtils::cleanPath(info.absoluteFilePath());
                }
            }
        }

        p_func(files);

        p_event->acceptProposedAction();
        return true;
    }

    return false;
}
