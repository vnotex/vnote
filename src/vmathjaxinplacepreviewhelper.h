#ifndef VMATHJAXINPLACEPREVIEWHELPER_H
#define VMATHJAXINPLACEPREVIEWHELPER_H

#include <QObject>

#include "hgmarkdownhighlighter.h"
#include "vpreviewmanager.h"
#include "vconstants.h"

class VEditor;
class VDocument;
class QTextDocument;
class VMathJaxPreviewHelper;

class MathjaxBlockPreviewInfo
{
public:
    MathjaxBlockPreviewInfo();

    explicit MathjaxBlockPreviewInfo(const VMathjaxBlock &p_mb);

    void updateInplacePreview(const VEditor *p_editor,
                              const QTextDocument *p_doc,
                              const QPixmap &p_image);

    VMathjaxBlock &mathjaxBlock()
    {
        return m_mathjaxBlock;
    }

    const VMathjaxBlock &mathjaxBlock() const
    {
        return m_mathjaxBlock;
    }

    bool inplacePreviewReady() const
    {
        return !m_inplacePreview.isNull();
    }

    const QSharedPointer<VImageToPreview> inplacePreview() const
    {
        return m_inplacePreview;
    }

private:
    static int getImageIndex()
    {
        static int index = 0;
        return ++index;
    }

    VMathjaxBlock m_mathjaxBlock;

    QSharedPointer<VImageToPreview> m_inplacePreview;
};


class VMathJaxInplacePreviewHelper : public QObject
{
    Q_OBJECT
public:
    VMathJaxInplacePreviewHelper(VEditor *p_editor,
                                 VDocument *p_document,
                                 QObject *p_parent = nullptr);

    void setEnabled(bool p_enabled);

public slots:
    void updateMathjaxBlocks(const QVector<VMathjaxBlock> &p_blocks);

signals:
    void inplacePreviewMathjaxBlockUpdated(const QVector<QSharedPointer<VImageToPreview> > &p_images);

    void checkBlocksForObsoletePreview(const QList<int> &p_blocks);

private slots:
    void mathjaxPreviewResultReady(int p_identitifer,
                                   int p_id,
                                   TimeStamp p_timeStamp,
                                   const QString &p_format,
                                   const QByteArray &p_data);

    void textToHtmlFinished(int p_identitifer, int p_id, int p_timeStamp, const QString &p_html);

private:
    struct MathjaxImageCacheEntry
    {
        MathjaxImageCacheEntry()
            : m_ts(0)
        {
        }

        MathjaxImageCacheEntry(TimeStamp p_ts,
                               const QByteArray &p_dataBa,
                               const QString &p_format)
            : m_ts(p_ts)
        {
            if (!p_dataBa.isEmpty()) {
                m_image.loadFromData(p_dataBa, p_format.toLocal8Bit().data());
            }
        }

        TimeStamp m_ts;
        QPixmap m_image;
    };


    void processForInplacePreview(int p_idx);

    // Emit signal to update inplace preview.
    void updateInplacePreview();

    bool textToHtmlViaWebView(const QString &p_text,
                              int p_id,
                              int p_timeStamp);

    void clearObsoleteCache();

    VEditor *m_editor;

    VDocument *m_document;

    QTextDocument *m_doc;

    bool m_enabled;

    VMathJaxPreviewHelper *m_mathJaxHelper;

    // Identification for VMathJaxPreviewHelper.
    int m_mathJaxID;

    int m_lastInplacePreviewSize;

    TimeStamp m_timeStamp;

    // Sorted by m_blockNumber in ascending order.
    QVector<MathjaxBlockPreviewInfo> m_mathjaxBlocks;

    int m_documentID;

    // Indexed by content.
    QHash<QString, QSharedPointer<MathjaxImageCacheEntry>> m_cache;
};

#endif // VMATHJAXINPLACEPREVIEWHELPER_H
