#include "attachmentdragdropareaindicator.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>

#include "viewwindow.h"
#include <buffer/buffer.h>
#include <utils/pathutils.h>
#include <utils/urldragdroputils.h>
#include <vnotex.h>

using namespace vnotex;

AttachmentDragDropAreaIndicator::AttachmentDragDropAreaIndicator(ViewWindow *p_viewWindow)
    : m_viewWindow(p_viewWindow)
{
}

bool AttachmentDragDropAreaIndicator::handleDragEnterEvent(QDragEnterEvent *p_event)
{
    if (isAccepted(p_event)) {
        p_event->acceptProposedAction();
        return true;
    }
    return false;
}

bool AttachmentDragDropAreaIndicator::handleDropEvent(QDropEvent *p_event)
{
    return UrlDragDropUtils::handleDropEvent(p_event, [this](const QStringList &p_files) {
                auto buffer = m_viewWindow->getBuffer();
                Q_ASSERT(buffer && buffer->isAttachmentSupported());
                auto files = buffer->addAttachment(QString(), p_files);
                if (!files.isEmpty()) {
                    VNoteX::getInst().showStatusMessageShort(
                        ViewWindow::tr("Attached %n file(s)", "", files.size()));
                }
            });
}

bool AttachmentDragDropAreaIndicator::isAccepted(const QDragEnterEvent *p_event)
{
    return p_event->mimeData()->hasFormat(QStringLiteral("text/uri-list"));
}
