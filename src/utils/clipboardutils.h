#ifndef CLIPBOARDUTILS_H
#define CLIPBOARDUTILS_H

#include <memory>

#include <QClipboard>

namespace vnotex
{
    class ClipboardUtils
    {
    public:
        ClipboardUtils() = delete;

        static QString getTextFromClipboard();

        static void setTextToClipboard(const QString &p_text);

        // @p_mimeData will be owned by utils.
        static void setMimeDataToClipboard(QClipboard *p_clipboard,
                                           QMimeData *p_mimeData,
                                           QClipboard::Mode p_mode = QClipboard::Clipboard);

        static void clearClipboard();

        static std::unique_ptr<QMimeData> cloneMimeData(const QMimeData *p_mimeData);

        static void setImageToClipboard(QClipboard *p_clipboard,
                                        const QImage &p_image,
                                        QClipboard::Mode p_mode = QClipboard::Clipboard);

    private:
        static bool mimeDataEquals(const QMimeData *p_a, const QMimeData *p_b);

        static void setMimeDataLoop(QClipboard *p_clipboard,
                                    QMimeData *p_mimeData,
                                    QClipboard::Mode p_mode);

        static void setImageLoop(QClipboard *p_clipboard,
                                 const QImage &p_image,
                                 QClipboard::Mode p_mode);
    };
} // ns vnotex

#endif // CLIPBOARDUTILS_H
