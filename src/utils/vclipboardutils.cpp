#include "vclipboardutils.h"

#include <QDebug>

#include "vutils.h"

void VClipboardUtils::setImageToClipboard(QClipboard *p_clipboard,
                                          const QImage &p_image,
                                          QClipboard::Mode p_mode)
{
#if defined(Q_OS_WIN)
    // On Windows, setImage() may fail. We will repeatedly retry until succeed.
    setImageLoop(p_clipboard, p_image, p_mode);
#else
    p_clipboard->setImage(p_image, p_mode);
#endif
}

void VClipboardUtils::setImageLoop(QClipboard *p_clipboard,
                                   const QImage &p_image,
                                   QClipboard::Mode p_mode)
{
    while (true) {
        p_clipboard->setImage(p_image, p_mode);

        QImage image = p_clipboard->image(p_mode);
        if (!image.isNull()) {
            break;
        }

        qDebug() << "fail to set image, retry";

        VUtils::sleepWait(100 /* ms */);
    }
}
