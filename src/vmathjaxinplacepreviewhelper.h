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

    void clearImageData()
    {
        m_imgDataBa.clear();
        m_inplacePreview.clear();
    }

    void updateNonContent(const QTextDocument *p_doc,
                          const VEditor *p_editor,
                          const VMathjaxBlock &p_mb);

    void updateInplacePreview(const VEditor *p_editor, const QTextDocument *p_doc);

    VMathjaxBlock &mathjaxBlock()
    {
        return m_mathjaxBlock;
    }

    const VMathjaxBlock &mathjaxBlock() const
    {
        return m_mathjaxBlock;
    }

    void setMathjaxBlock(const VMathjaxBlock &p_mb)
    {
        m_mathjaxBlock = p_mb;
        clearImageData();
    }

    bool inplacePreviewReady() const
    {
        return !m_inplacePreview.isNull();
    }

    void setImageDataBa(const QString &p_format, const QByteArray &p_data)
    {
        m_imgFormat = p_format;
        m_imgDataBa = p_data;
    }

    bool hasImageDataBa() const
    {
        return !m_imgDataBa.isEmpty();
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

    QByteArray m_imgDataBa;

    QString m_imgFormat;

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
    void processForInplacePreview(int p_idx);

    // Emit signal to update inplace preview.
    void updateInplacePreview();

    void textToHtmlViaWebView(const QString &p_text,
                              int p_id,
                              int p_timeStamp);

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
};

#endif // VMATHJAXINPLACEPREVIEWHELPER_H
