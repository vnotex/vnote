#ifndef PREVIEWHELPER_H
#define PREVIEWHELPER_H

#include <QObject>
#include <QPixmap>

#include <vtextedit/global.h>
#include <vtextedit/lrucache.h>
#include <core/global.h>
#include "markdownvieweradapter.h"

class QTimer;
class QTextDocument;

namespace vte
{
    namespace peg
    {
        struct FencedCodeBlock;
        struct MathBlock;
    }

    struct PreviewItem;
}

namespace vnotex
{
    class MarkdownEditor;

    // Helper to manage in-place preview and focus preview.
    class PreviewHelper : public QObject
    {
        Q_OBJECT
    public:
        enum SourceFlag
        {
            FlowChart = 0x1,
            Mermaid = 0x2,
            WaveDrom = 0x4,
            PlantUml = 0x8,
            Graphviz = 0x10,
            Math = 0x20
        };
        Q_DECLARE_FLAGS(SourceFlags, SourceFlag);

        PreviewHelper(MarkdownEditor *p_editor, QObject *p_parent = nullptr);

        void setMarkdownEditor(MarkdownEditor *p_editor);

    public slots:
        void codeBlocksUpdated(vte::TimeStamp p_timeStamp,
                               const QVector<vte::peg::FencedCodeBlock> &p_codeBlocks);

        void mathBlocksUpdated(const QVector<vte::peg::MathBlock> &p_mathBlocks);

        void handleGraphPreviewData(const MarkdownViewerAdapter::PreviewData &p_data);

        void handleMathPreviewData(const MarkdownViewerAdapter::PreviewData &p_data);

    signals:
        // Request to preview graph.
        // There must be a corresponding call to handleGraphPreviewData().
        void graphPreviewRequested(quint64 p_id,
                                   TimeStamp p_timeStamp,
                                   const QString &p_lang,
                                   const QString &p_text);

        // Request to preview math.
        // There must be a corresponding call to handleMathPreviewData().
        void mathPreviewRequested(quint64 p_id,
                                  TimeStamp p_timeStamp,
                                  const QString &p_text);

        // Request to do in-place preview for @p_previewItems.
        void inplacePreviewCodeBlockUpdated(const QVector<QSharedPointer<vte::PreviewItem>> &p_previewItems);

        // Request to do in-place preview for @p_previewItems.
        void inplacePreviewMathBlockUpdated(const QVector<QSharedPointer<vte::PreviewItem>> &p_previewItems);

        void potentialObsoletePreviewBlocksUpdated(const QList<int> &p_blocks);

    private:
        // Preview data of each related code block.
        struct CodeBlockPreviewData
        {
            CodeBlockPreviewData() = default;

            CodeBlockPreviewData(const vte::peg::FencedCodeBlock &p_codeBlock);

            void updateInplacePreview(QTextDocument *p_doc,
                                      const QPixmap &p_image,
                                      const QString &p_imageName,
                                      QRgb p_background,
                                      int p_tabStopWidth);

            // Start and end block of the fenced code block.
            int m_startBlock = 0;
            int m_endBlock = 0;

            QString m_lang;

            // Including the fence text.
            // Will be filled only when preview is needed.
            QString m_text;

            QSharedPointer<vte::PreviewItem> m_inplacePreview;
        };

        struct MathBlockPreviewData
        {
            MathBlockPreviewData() = default;

            MathBlockPreviewData(const vte::peg::MathBlock &p_mathBlock);

            void updateInplacePreview(QTextDocument *p_doc,
                                      const QPixmap &p_image,
                                      const QString &p_imageName,
                                      int p_tabStopWidth);

            // Block number for in-place preview.
            int m_blockNumber = -1;

            // Whether it should be previewed as block or not.
            bool m_previewedAsBlock = false;

            // Start index wihtin block with number m_blockNumber, including the start mark.
            int m_index = -1;

            // Length of this math in block with number m_blockNumber, including the end mark.
            int m_length = -1;

            // Including the guarding marks.
            // Will be filled only when preview is needed.
            QString m_text;

            QSharedPointer<vte::PreviewItem> m_inplacePreview;
        };

        // Data of the preview result.
        struct GraphPreviewData
        {
            GraphPreviewData() = default;

            GraphPreviewData(TimeStamp p_timeStamp,
                             const QString &p_format,
                             const QByteArray &p_data,
                             QRgb p_background = 0x0,
                             qreal p_scaleFactor = 1);

            bool isNull() const;

            TimeStamp m_timeStamp = 0;

            QPixmap m_image;

            // Name of the image for identification in resource manager.
            QString m_name;

            // Background color to override.
            // 0x0 indicates it is not specified.
            QRgb m_background = 0x0;

            // An increasing index to used as the image name.
            static int s_imageIndex;
        };

        // Return <InplacePreview, FocusPreview>.
        QPair<bool, bool> isLangNeedPreview(const QString &p_lang) const;

        bool isInplacePreviewSourceEnabled(SourceFlag p_flag) const;

        bool checkPreviewSourceLang(SourceFlag p_flag, const QString &p_lang) const;

        // Inplace preview code block m_codeBlocksData[@p_blockPreviewIdx].
        void inplacePreviewCodeBlock(int p_blockPreviewIdx);

        void inplacePreviewMathBlock(int p_blockPreviewIdx);

        void updateEditorInplacePreviewCodeBlock();

        void updateEditorInplacePreviewMathBlock();

        qreal getEditorScaleFactor() const;

        MarkdownEditor *m_editor = nullptr;

        QTextDocument *m_document = nullptr;

        // Need to init it in the constructor.
        SourceFlags m_inplacePreviewSources;

        bool m_inplacePreviewEnabled = true;

        TimeStamp m_codeBlockTimeStamp = 0;

        TimeStamp m_mathBlockTimeStamp = 0;

        // Sorted by startBlock in ascending order.
        QVector<CodeBlockPreviewData> m_codeBlocksData;

        QVector<MathBlockPreviewData> m_mathBlocksData;

        // Tab stop width of the editor, used for block margin calculation.
        int m_tabStopWidth = 4;

        // To record the size of previous inplace preview of code block.
        int m_previousInplacePreviewCodeBlockSize = 0;

        // To record the size of previous inplace preview of math block.
        int m_previousInplacePreviewMathBlockSize = 0;

        // {text} -> GraphPreviewData.
        vte::LruCache<QString, QSharedPointer<GraphPreviewData>> m_codeBlockCache;

        vte::LruCache<QString, QSharedPointer<GraphPreviewData>> m_mathBlockCache;
    };
}

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::PreviewHelper::SourceFlags)

#endif // PREVIEWHELPER_H
