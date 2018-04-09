#ifndef VLIVEPREVIEWHELPER_H
#define VLIVEPREVIEWHELPER_H

#include <QObject>
#include <QTextDocument>

#include "hgmarkdownhighlighter.h"
#include "vpreviewmanager.h"

class VEditor;
class VDocument;
class VGraphvizHelper;
class VPlantUMLHelper;

class CodeBlockPreviewInfo
{
public:
    CodeBlockPreviewInfo();

    explicit CodeBlockPreviewInfo(const VCodeBlock &p_cb);

    void clearImageData();

    void updateNonContent(const QTextDocument *p_doc, const VCodeBlock &p_cb);

    void updateInplacePreview(const VEditor *p_editor, const QTextDocument *p_doc);

    VCodeBlock &codeBlock()
    {
        return m_codeBlock;
    }

    const VCodeBlock &codeBlock() const
    {
        return m_codeBlock;
    }

    void setCodeBlock(const VCodeBlock &p_cb)
    {
        m_codeBlock = p_cb;

        clearImageData();
    }

    bool inplacePreviewReady() const
    {
        return !m_inplacePreview.isNull();
    }

    bool hasImageData() const
    {
        return !m_imgData.isEmpty();
    }

    const QString &imageData() const
    {
        return m_imgData;
    }

    const QString &imageFormat() const
    {
        return m_imgFormat;
    }

    void setImageData(const QString &p_format, const QString &p_data)
    {
        m_imgFormat = p_format;
        m_imgData = p_data;
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

    VCodeBlock m_codeBlock;

    QString m_imgData;

    QString m_imgFormat;

    QSharedPointer<VImageToPreview> m_inplacePreview;
};


// Manage live preview and inplace preview.
class VLivePreviewHelper : public QObject
{
    Q_OBJECT
public:
    VLivePreviewHelper(VEditor *p_editor,
                       VDocument *p_document,
                       QObject *p_parent = nullptr);

    void updateLivePreview();

    void setLivePreviewEnabled(bool p_enabled);

    void setInplacePreviewEnabled(bool p_enabled);

    bool isPreviewEnabled() const;

public slots:
    void updateCodeBlocks(const QVector<VCodeBlock> &p_codeBlocks);

    void webAsyncResultReady(int p_id, const QString &p_lang, const QString &p_result);

signals:
    void inplacePreviewCodeBlockUpdated(const QVector<QSharedPointer<VImageToPreview> > &p_images);

private slots:
    void handleCursorPositionChanged();

    void localAsyncResultReady(int p_id, const QString &p_format, const QString &p_result);

private:

    bool isPreviewLang(const QString &p_lang) const;

    // Get image data for this code block for inplace preview.
    void processForInplacePreview(int p_idx);

    // Emit signal to update inplace preview.
    void updateInplacePreview();

    // Sorted by m_startBlock in ascending order.
    QVector<CodeBlockPreviewInfo> m_codeBlocks;

    VEditor *m_editor;

    VDocument *m_document;

    QTextDocument *m_doc;

    // Current previewed code block index in m_codeBlocks.
    int m_cbIndex;

    bool m_flowchartEnabled;
    bool m_mermaidEnabled;
    int m_plantUMLMode;
    bool m_graphvizEnabled;
    bool m_mathjaxEnabled;

    bool m_livePreviewEnabled;

    bool m_inplacePreviewEnabled;

    VGraphvizHelper *m_graphvizHelper;
    VPlantUMLHelper *m_plantUMLHelper;
};

inline bool VLivePreviewHelper::isPreviewEnabled() const
{
    return m_inplacePreviewEnabled || m_livePreviewEnabled;
}
#endif // VLIVEPREVIEWHELPER_H
