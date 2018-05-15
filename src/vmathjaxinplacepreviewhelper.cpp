#include "vmathjaxinplacepreviewhelper.h"

#include <QDebug>

#include "veditor.h"
#include "vdocument.h"
#include "vmainwindow.h"
#include "veditarea.h"
#include "vmathjaxpreviewhelper.h"

extern VMainWindow *g_mainWin;

MathjaxBlockPreviewInfo::MathjaxBlockPreviewInfo()
{
}

MathjaxBlockPreviewInfo::MathjaxBlockPreviewInfo(const VMathjaxBlock &p_mb)
    : m_mathjaxBlock(p_mb)
{
}

void MathjaxBlockPreviewInfo::updateNonContent(const QTextDocument *p_doc,
                                               const VEditor *p_editor,
                                               const VMathjaxBlock &p_mb)
{
    m_mathjaxBlock.updateNonContent(p_mb);
    if (m_inplacePreview.isNull()) {
        return;
    }

    QTextBlock block = p_doc->findBlockByNumber(m_mathjaxBlock.m_blockNumber);
    if (block.isValid()) {
        m_inplacePreview->m_startPos = block.position() + m_mathjaxBlock.m_index;
        m_inplacePreview->m_endPos = m_inplacePreview->m_startPos + m_mathjaxBlock.m_length;
        m_inplacePreview->m_blockPos = block.position();
        m_inplacePreview->m_blockNumber = m_mathjaxBlock.m_blockNumber;
        // Padding may changed.
        m_inplacePreview->m_padding = VPreviewManager::calculateBlockMargin(block,
                                                                            p_editor->tabStopWidthW());
        m_inplacePreview->m_isBlock = m_mathjaxBlock.m_previewedAsBlock;
    } else {
        m_inplacePreview.clear();
    }
}

void MathjaxBlockPreviewInfo::updateInplacePreview(const VEditor *p_editor,
                                                   const QTextDocument *p_doc)
{
    QTextBlock block = p_doc->findBlockByNumber(m_mathjaxBlock.m_blockNumber);
    if (block.isValid()) {
        VImageToPreview *preview = new VImageToPreview();

        preview->m_startPos = block.position() + m_mathjaxBlock.m_index;
        preview->m_endPos = preview->m_startPos + m_mathjaxBlock.m_length;
        preview->m_blockPos = block.position();
        preview->m_blockNumber = m_mathjaxBlock.m_blockNumber;
        preview->m_padding = VPreviewManager::calculateBlockMargin(block,
                                                                   p_editor->tabStopWidthW());
        preview->m_name = QString::number(getImageIndex());
        preview->m_isBlock = m_mathjaxBlock.m_previewedAsBlock;

        if (hasImageDataBa()) {
            preview->m_image.loadFromData(m_imgDataBa,
                                                   m_imgFormat.toLocal8Bit().data());
        } else {
            preview->m_image = QPixmap();
        }

        m_inplacePreview.reset(preview);
    } else {
        m_inplacePreview.clear();
    }
}

VMathJaxInplacePreviewHelper::VMathJaxInplacePreviewHelper(VEditor *p_editor,
                                                           VDocument *p_document,
                                                           QObject *p_parent)
    : QObject(p_parent),
      m_editor(p_editor),
      m_document(p_document),
      m_doc(p_editor->documentW()),
      m_enabled(false),
      m_lastInplacePreviewSize(0),
      m_timeStamp(0)
{
    m_mathJaxHelper = g_mainWin->getEditArea()->getMathJaxPreviewHelper();
    m_mathJaxID = m_mathJaxHelper->registerIdentifier();
    connect(m_mathJaxHelper, &VMathJaxPreviewHelper::mathjaxPreviewResultReady,
            this, &VMathJaxInplacePreviewHelper::mathjaxPreviewResultReady);

    m_documentID = m_document->registerIdentifier();
    connect(m_document, &VDocument::textToHtmlFinished,
            this, &VMathJaxInplacePreviewHelper::textToHtmlFinished);
}

void VMathJaxInplacePreviewHelper::setEnabled(bool p_enabled)
{
    if (m_enabled != p_enabled) {
        m_enabled = p_enabled;

        if (!m_enabled) {
            m_mathjaxBlocks.clear();
        }

        updateInplacePreview();
    }
}

void VMathJaxInplacePreviewHelper::updateMathjaxBlocks(const QVector<VMathjaxBlock> &p_blocks)
{
    if (!m_enabled) {
        return;
    }

    ++m_timeStamp;

    int idx = 0;
    bool manualUpdate = true;
    for (auto const & vmb : p_blocks) {
        bool cached = false;
        if (idx < m_mathjaxBlocks.size()) {
            MathjaxBlockPreviewInfo &mb = m_mathjaxBlocks[idx];
            if (mb.mathjaxBlock().equalContent(vmb)) {
                cached = true;
                mb.updateNonContent(m_doc, m_editor, vmb);
            } else {
                mb.setMathjaxBlock(vmb);
            }
        } else {
            m_mathjaxBlocks.append(MathjaxBlockPreviewInfo(vmb));
        }

        if (m_enabled
            && (!cached || !m_mathjaxBlocks[idx].inplacePreviewReady())) {
            manualUpdate = false;
            processForInplacePreview(idx);
        }

        ++idx;
    }

    m_mathjaxBlocks.resize(idx);

    if (manualUpdate) {
        updateInplacePreview();
    }
}

void VMathJaxInplacePreviewHelper::processForInplacePreview(int p_idx)
{
    MathjaxBlockPreviewInfo &mb = m_mathjaxBlocks[p_idx];
    const VMathjaxBlock &vmb = mb.mathjaxBlock();
    if (vmb.m_text.isEmpty()) {
        updateInplacePreview();
    } else {
        if (!textToHtmlViaWebView(vmb.m_text, p_idx, m_timeStamp)) {
            updateInplacePreview();
        }
    }
}

bool VMathJaxInplacePreviewHelper::textToHtmlViaWebView(const QString &p_text,
                                                        int p_id,
                                                        int p_timeStamp)
{
    if (!m_document->isReadyToTextToHtml()) {
        qDebug() << "web side is not ready to convert text to HTML";
        return false;
    }

    m_document->textToHtmlAsync(m_documentID, p_id, p_timeStamp, p_text, false);
    return true;
}

void VMathJaxInplacePreviewHelper::updateInplacePreview()
{
    QSet<int> blocks;
    QVector<QSharedPointer<VImageToPreview> > images;
    for (int i = 0; i < m_mathjaxBlocks.size(); ++i) {
        MathjaxBlockPreviewInfo &mb = m_mathjaxBlocks[i];
        if (mb.inplacePreviewReady()) {
            if (!mb.inplacePreview()->m_image.isNull()) {
                images.append(mb.inplacePreview());
            } else {
                blocks.insert(mb.inplacePreview()->m_blockNumber);
            }
        } else {
            blocks.insert(mb.mathjaxBlock().m_blockNumber);
        }
    }

    if (images.isEmpty() && m_lastInplacePreviewSize == 0) {
        return;
    }

    emit inplacePreviewMathjaxBlockUpdated(images);

    m_lastInplacePreviewSize = images.size();

    if (!blocks.isEmpty()) {
        emit checkBlocksForObsoletePreview(blocks.toList());
    }
}

void VMathJaxInplacePreviewHelper::mathjaxPreviewResultReady(int p_identitifer,
                                                             int p_id,
                                                             TimeStamp p_timeStamp,
                                                             const QString &p_format,
                                                             const QByteArray &p_data)
{
    if (p_identitifer != m_mathJaxID || p_timeStamp != m_timeStamp) {
        return;
    }

    if (p_id >= m_mathjaxBlocks.size() || p_data.isEmpty()) {
        updateInplacePreview();
        return;
    }

    MathjaxBlockPreviewInfo &mb = m_mathjaxBlocks[p_id];
    mb.setImageDataBa(p_format, p_data);
    mb.updateInplacePreview(m_editor, m_doc);
    updateInplacePreview();
}

void VMathJaxInplacePreviewHelper::textToHtmlFinished(int p_identitifer,
                                                      int p_id,
                                                      int p_timeStamp,
                                                      const QString &p_html)
{
    if (m_documentID != p_identitifer || m_timeStamp != (TimeStamp)p_timeStamp) {
        return;
    }

    Q_ASSERT(p_html.startsWith("<"));
    m_mathJaxHelper->previewMathJaxFromHtml(m_mathJaxID,
                                            p_id,
                                            p_timeStamp,
                                            p_html);
}
