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

void MathjaxBlockPreviewInfo::updateInplacePreview(const VEditor *p_editor,
                                                   const QTextDocument *p_doc,
                                                   const QPixmap &p_image)
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

        preview->m_image = p_image;

        m_inplacePreview.reset(preview);
    } else {
        m_inplacePreview.clear();
    }
}

#define MATHJAX_IMAGE_CACHE_SIZE_DIFF 20
#define MATHJAX_IMAGE_CACHE_TIME_DIFF 5

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
            m_cache.clear();
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

    m_mathjaxBlocks.clear();
    m_mathjaxBlocks.reserve(p_blocks.size());
    bool manualUpdate = true;
    for (int i = 0; i < p_blocks.size(); ++i) {
        const VMathjaxBlock &vmb = p_blocks[i];
        const QString &text = vmb.m_text;
        bool cached = false;

        m_mathjaxBlocks.append(MathjaxBlockPreviewInfo(vmb));

        auto it = m_cache.find(text);
        if (it != m_cache.end()) {
            QSharedPointer<MathjaxImageCacheEntry> &entry = it.value();
            entry->m_ts = m_timeStamp;
            cached = true;
            m_mathjaxBlocks.last().updateInplacePreview(m_editor, m_doc, entry->m_image);
        }

        if (!cached || !m_mathjaxBlocks.last().inplacePreviewReady()) {
            manualUpdate = false;
            processForInplacePreview(m_mathjaxBlocks.size() - 1);
        }
    }

    if (manualUpdate) {
        updateInplacePreview();
    }

    clearObsoleteCache();
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
    // Update the cache.
    QSharedPointer<MathjaxImageCacheEntry> entry(new MathjaxImageCacheEntry(p_timeStamp,
                                                                            p_data,
                                                                            p_format));
    m_cache.insert(mb.mathjaxBlock().m_text, entry);
    mb.updateInplacePreview(m_editor, m_doc, entry->m_image);

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

void VMathJaxInplacePreviewHelper::clearObsoleteCache()
{
    if (m_cache.size() - m_mathjaxBlocks.size() <= MATHJAX_IMAGE_CACHE_SIZE_DIFF) {
        return;
    }

    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (m_timeStamp - it.value()->m_ts > MATHJAX_IMAGE_CACHE_TIME_DIFF) {
            it.value().clear();
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}
