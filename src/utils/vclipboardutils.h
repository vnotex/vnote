#ifndef VCLIPBOARDUTILS_H
#define VCLIPBOARDUTILS_H

#include <QImage>
#include <QClipboard>

class QMimeData;


class VClipboardUtils
{
public:
    static void setImageToClipboard(QClipboard *p_clipboard,
                                    const QImage &p_image,
                                    QClipboard::Mode p_mode = QClipboard::Clipboard);

    static void setMimeDataToClipboard(QClipboard *p_clipboard,
                                       QMimeData *p_mimeData,
                                       QClipboard::Mode p_mode = QClipboard::Clipboard);

private:
    VClipboardUtils()
    {
    }

    static void setImageLoop(QClipboard *p_clipboard,
                             const QImage &p_image,
                             QClipboard::Mode p_mode);

    static void setMimeDataLoop(QClipboard *p_clipboard,
                                QMimeData *p_mimeData,
                                QClipboard::Mode p_mode);
};

#endif // VCLIPBOARDUTILS_H
