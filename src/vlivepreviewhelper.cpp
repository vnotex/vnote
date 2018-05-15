#include "vlivepreviewhelper.h"

#include <QByteArray>

#include "veditor.h"
#include "vdocument.h"
#include "vconfigmanager.h"
#include "vgraphvizhelper.h"
#include "vplantumlhelper.h"
#include "vcodeblockhighlighthelper.h"
#include "vmainwindow.h"
#include "veditarea.h"
#include "vmathjaxpreviewhelper.h"

extern VConfigManager *g_config;

extern VMainWindow *g_mainWin;

// Use the highest 4 bits (31-28) to indicate the lang.
#define LANG_PREFIX_GRAPHVIZ 0x10000000UL
#define LANG_PREFIX_PLANTUML 0x20000000UL
#define LANG_PREFIX_MATHJAX  0x30000000UL
#define LANG_PREFIX_MASK 0xf0000000UL

// Use th 27th bit to indicate the preview type.
#define TYPE_LIVE_PREVIEW 0x0UL
#define TYPE_INPLACE_PREVIEW 0x08000000UL
#define TYPE_MASK 0x08000000UL

#define INDEX_MASK 0x00ffffffUL

CodeBlockPreviewInfo::CodeBlockPreviewInfo()
{
}

CodeBlockPreviewInfo::CodeBlockPreviewInfo(const VCodeBlock &p_cb)
    : m_codeBlock(p_cb)
{
}

void CodeBlockPreviewInfo::updateInplacePreview(const VEditor *p_editor,
                                                const QTextDocument *p_doc,
                                                const QPixmap &p_image)
{
    QTextBlock block = p_doc->findBlockByNumber(m_codeBlock.m_endBlock);
    if (block.isValid()) {
        VImageToPreview *preview = new VImageToPreview();

        preview->m_startPos = block.position();
        preview->m_endPos = block.position() + block.length();
        preview->m_blockPos = block.position();
        preview->m_blockNumber = m_codeBlock.m_endBlock;
        preview->m_padding = VPreviewManager::calculateBlockMargin(block,
                                                                   p_editor->tabStopWidthW());
        preview->m_name = QString::number(getImageIndex());
        preview->m_isBlock = true;

        preview->m_image = p_image;

        m_inplacePreview.reset(preview);
    } else {
        m_inplacePreview.clear();
    }
}


#define CODE_BLOCK_IMAGE_CACHE_SIZE_DIFF 10
#define CODE_BLOCK_IMAGE_CACHE_TIME_DIFF 5

VLivePreviewHelper::VLivePreviewHelper(VEditor *p_editor,
                                       VDocument *p_document,
                                       QObject *p_parent)
    : QObject(p_parent),
      m_editor(p_editor),
      m_document(p_document),
      m_doc(p_editor->documentW()),
      m_cbIndex(-1),
      m_livePreviewEnabled(false),
      m_inplacePreviewEnabled(false),
      m_graphvizHelper(NULL),
      m_plantUMLHelper(NULL),
      m_lastInplacePreviewSize(0),
      m_timeStamp(0),
      m_scaleFactor(VUtils::calculateScaleFactor())
{
    connect(m_editor->object(), &VEditorObject::cursorPositionChanged,
            this, &VLivePreviewHelper::handleCursorPositionChanged);

    m_flowchartEnabled = g_config->getEnableFlowchart();
    m_mermaidEnabled = g_config->getEnableMermaid();
    m_plantUMLMode = g_config->getPlantUMLMode();
    m_graphvizEnabled = g_config->getEnableGraphviz();
    m_mathjaxEnabled = g_config->getEnableMathjax();

    m_mathJaxHelper = g_mainWin->getEditArea()->getMathJaxPreviewHelper();
    m_mathJaxID = m_mathJaxHelper->registerIdentifier();
    connect(m_mathJaxHelper, &VMathJaxPreviewHelper::mathjaxPreviewResultReady,
            this, &VLivePreviewHelper::mathjaxPreviewResultReady);
    connect(m_mathJaxHelper, &VMathJaxPreviewHelper::diagramPreviewResultReady,
            // The same handle logics.
            this, &VLivePreviewHelper::mathjaxPreviewResultReady);
}

void VLivePreviewHelper::checkLang(const QString &p_lang,
                                   bool &p_livePreview,
                                   bool &p_inplacePreview) const
{
    if (m_flowchartEnabled && (p_lang == "flow" || p_lang == "flowchart")) {
        p_livePreview = p_inplacePreview = true;
    } else if (m_plantUMLMode != PlantUMLMode::DisablePlantUML && p_lang == "puml") {
        p_livePreview = true;
        p_inplacePreview = m_plantUMLMode == PlantUMLMode::LocalPlantUML;
    } else if (m_graphvizEnabled && p_lang == "dot") {
        p_livePreview = p_inplacePreview = true;
    } else if (m_mermaidEnabled && p_lang == "mermaid") {
        p_livePreview = true;
        p_inplacePreview = false;
    } else if (m_mathjaxEnabled && p_lang == "mathjax") {
        p_livePreview = false;
        p_inplacePreview = true;
    } else {
        p_livePreview = p_inplacePreview = false;
    }
}

void VLivePreviewHelper::updateCodeBlocks(const QVector<VCodeBlock> &p_codeBlocks)
{
    if (!m_livePreviewEnabled && !m_inplacePreviewEnabled) {
        return;
    }

    ++m_timeStamp;

    int lastIndex = m_cbIndex;
    m_cbIndex = -1;
    int cursorBlock = m_editor->textCursorW().block().blockNumber();
    bool needUpdate = m_livePreviewEnabled;
    bool manualInplacePreview = m_inplacePreviewEnabled;
    m_codeBlocks.clear();

    for (int i = 0; i < p_codeBlocks.size(); ++i) {
        const VCodeBlock &vcb = p_codeBlocks[i];
        bool livePreview = false, inplacePreview = false;
        checkLang(vcb.m_lang, livePreview, inplacePreview);
        if (!livePreview && !inplacePreview) {
            continue;
        }

        const QString &text = vcb.m_text;
        bool cached = false;

        m_codeBlocks.append(CodeBlockPreviewInfo(vcb));
        int idx = m_codeBlocks.size() - 1;

        auto it = m_cache.find(text);
        if (it != m_cache.end()) {
            QSharedPointer<CodeBlockImageCacheEntry> &entry = it.value();
            entry->m_ts = m_timeStamp;
            cached = true;
            m_codeBlocks[idx].setImageData(entry->m_imgFormat, entry->m_imgData);
            m_codeBlocks[idx].updateInplacePreview(m_editor, m_doc, entry->m_image);
        }

        if (m_inplacePreviewEnabled
            && inplacePreview
            && (!cached || !m_codeBlocks[idx].inplacePreviewReady())) {
            manualInplacePreview = false;
            processForInplacePreview(idx);
        }

        if (m_livePreviewEnabled
            && livePreview
            && vcb.m_startBlock <= cursorBlock
            && vcb.m_endBlock >= cursorBlock) {
            if (lastIndex == idx && cached) {
                needUpdate = false;
            }

            m_cbIndex = idx;
        }
    }

    if (manualInplacePreview) {
        updateInplacePreview();
    }

    if (needUpdate) {
        updateLivePreview();
    }

    clearObsoleteCache();
}

void VLivePreviewHelper::handleCursorPositionChanged()
{
    if (!m_livePreviewEnabled || m_codeBlocks.isEmpty()) {
        return;
    }

    int cursorBlock = m_editor->textCursorW().block().blockNumber();

    int left = 0, right = m_codeBlocks.size() - 1;
    int mid = left;
    while (left <= right) {
        mid = (left + right) / 2;
        const CodeBlockPreviewInfo &cb = m_codeBlocks[mid];
        const VCodeBlock &vcb = cb.codeBlock();
        if (vcb.m_startBlock <= cursorBlock && vcb.m_endBlock >= cursorBlock) {
            break;
        } else if (vcb.m_startBlock > cursorBlock) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    if (left <= right) {
        if (m_cbIndex != mid) {
            m_cbIndex = mid;
            updateLivePreview();
        }
    }
}

static QString removeFence(const QString &p_text)
{
    QString text = VCodeBlockHighlightHelper::unindentCodeBlock(p_text);
    Q_ASSERT(text.startsWith("```") && text.endsWith("```"));
    int idx = text.indexOf('\n') + 1;
    return text.mid(idx, text.size() - idx - 3);
}

void VLivePreviewHelper::updateLivePreview()
{
    if (m_cbIndex < 0) {
        return;
    }

    Q_ASSERT(!(m_cbIndex & ~INDEX_MASK));
    const CodeBlockPreviewInfo &cb = m_codeBlocks[m_cbIndex];
    const VCodeBlock &vcb = cb.codeBlock();
    if (vcb.m_lang == "dot") {
        if (!m_graphvizHelper) {
            m_graphvizHelper = new VGraphvizHelper(this);
            connect(m_graphvizHelper, &VGraphvizHelper::resultReady,
                    this, &VLivePreviewHelper::localAsyncResultReady);
        }

        if (!cb.hasImageData()) {
            m_graphvizHelper->processAsync(m_cbIndex | LANG_PREFIX_GRAPHVIZ | TYPE_LIVE_PREVIEW,
                                           m_timeStamp,
                                           "svg",
                                           removeFence(vcb.m_text));
        } else {
            m_document->setPreviewContent(vcb.m_lang, cb.imageData());
        }
    } else if (vcb.m_lang == "puml" && m_plantUMLMode == PlantUMLMode::LocalPlantUML) {
        if (!m_plantUMLHelper) {
            m_plantUMLHelper = new VPlantUMLHelper(this);
            connect(m_plantUMLHelper, &VPlantUMLHelper::resultReady,
                    this, &VLivePreviewHelper::localAsyncResultReady);
        }

        if (!cb.hasImageData()) {
            m_plantUMLHelper->processAsync(m_cbIndex | LANG_PREFIX_PLANTUML | TYPE_LIVE_PREVIEW,
                                           m_timeStamp,
                                           "svg",
                                           removeFence(vcb.m_text));
        } else {
            m_document->setPreviewContent(vcb.m_lang, cb.imageData());
        }
    } else if (vcb.m_lang != "mathjax") {
        // No need to live preview MathJax.
        m_document->previewCodeBlock(m_cbIndex,
                                     vcb.m_lang,
                                     removeFence(vcb.m_text),
                                     true);
    }
}

void VLivePreviewHelper::setLivePreviewEnabled(bool p_enabled)
{
    if (m_livePreviewEnabled == p_enabled) {
        return;
    }

    m_livePreviewEnabled = p_enabled;
    if (!m_livePreviewEnabled) {
        m_cbIndex = -1;
        m_document->previewCodeBlock(-1, "", "", true);

        if (!m_inplacePreviewEnabled) {
            m_codeBlocks.clear();
            m_cache.clear();
            updateInplacePreview();
        }
    }
}

void VLivePreviewHelper::setInplacePreviewEnabled(bool p_enabled)
{
    if (m_inplacePreviewEnabled == p_enabled) {
        return;
    }

    m_inplacePreviewEnabled = p_enabled;
    if (!m_inplacePreviewEnabled && !m_livePreviewEnabled) {
        m_codeBlocks.clear();
        m_cache.clear();
    }

    updateInplacePreview();
}

void VLivePreviewHelper::localAsyncResultReady(int p_id,
                                               TimeStamp p_timeStamp,
                                               const QString &p_format,
                                               const QString &p_result)
{
    if (p_timeStamp != m_timeStamp) {
        return;
    }

    Q_UNUSED(p_format);
    Q_ASSERT(p_format == "svg");
    int idx = p_id & INDEX_MASK;
    bool livePreview = (p_id & TYPE_MASK) == TYPE_LIVE_PREVIEW;
    QString lang;

    switch (p_id & LANG_PREFIX_MASK) {
    case LANG_PREFIX_PLANTUML:
        lang = "puml";
        break;

    case LANG_PREFIX_GRAPHVIZ:
        lang = "dot";
        break;

    default:
        return;
    }

    if (idx >= m_codeBlocks.size()) {
        return;
    }

    CodeBlockPreviewInfo &cb = m_codeBlocks[idx];
    const QString &text = cb.codeBlock().m_text;

    QSharedPointer<CodeBlockImageCacheEntry> entry(new CodeBlockImageCacheEntry(p_timeStamp,
                                                                                p_format,
                                                                                p_result,
                                                                                getScaleFactor(cb)));
    m_cache.insert(text, entry);

    cb.setImageData(p_format, p_result);
    cb.updateInplacePreview(m_editor, m_doc, entry->m_image);

    if (livePreview) {
        if (idx != m_cbIndex) {
            return;
        }

        m_document->setPreviewContent(lang, p_result);
    } else {
        // Inplace preview.
        updateInplacePreview();
    }
}

void VLivePreviewHelper::processForInplacePreview(int p_idx)
{
    CodeBlockPreviewInfo &cb = m_codeBlocks[p_idx];
    const VCodeBlock &vcb = cb.codeBlock();
    Q_ASSERT(!cb.hasImageData());
    if (vcb.m_lang == "dot") {
        if (!m_graphvizHelper) {
            m_graphvizHelper = new VGraphvizHelper(this);
            connect(m_graphvizHelper, &VGraphvizHelper::resultReady,
                    this, &VLivePreviewHelper::localAsyncResultReady);
        }

        m_graphvizHelper->processAsync(p_idx | LANG_PREFIX_GRAPHVIZ | TYPE_INPLACE_PREVIEW,
                                       m_timeStamp,
                                       "svg",
                                       removeFence(vcb.m_text));
    } else if (vcb.m_lang == "puml" && m_plantUMLMode == PlantUMLMode::LocalPlantUML) {
        if (!m_plantUMLHelper) {
            m_plantUMLHelper = new VPlantUMLHelper(this);
            connect(m_plantUMLHelper, &VPlantUMLHelper::resultReady,
                    this, &VLivePreviewHelper::localAsyncResultReady);
        }

        m_plantUMLHelper->processAsync(p_idx | LANG_PREFIX_PLANTUML | TYPE_INPLACE_PREVIEW,
                                       m_timeStamp,
                                       "svg",
                                       removeFence(vcb.m_text));
    } else if (vcb.m_lang == "flow"
               || vcb.m_lang == "flowchart") {
        m_mathJaxHelper->previewDiagram(m_mathJaxID,
                                        p_idx,
                                        m_timeStamp,
                                        vcb.m_lang,
                                        removeFence(vcb.m_text));
    } else if (vcb.m_lang == "mathjax") {
        m_mathJaxHelper->previewMathJax(m_mathJaxID,
                                        p_idx,
                                        m_timeStamp,
                                        removeFence(vcb.m_text));
    }
}

void VLivePreviewHelper::updateInplacePreview()
{
    QSet<int> blocks;
    QVector<QSharedPointer<VImageToPreview> > images;
    for (int i = 0; i < m_codeBlocks.size(); ++i) {
        CodeBlockPreviewInfo &cb = m_codeBlocks[i];
        if (cb.inplacePreviewReady()) {
            if (!cb.inplacePreview()->m_image.isNull()) {
                images.append(cb.inplacePreview());
            } else {
                blocks.insert(cb.inplacePreview()->m_blockNumber);
            }
        } else {
            blocks.insert(cb.codeBlock().m_endBlock);
        }
    }

    if (images.isEmpty() && m_lastInplacePreviewSize == 0) {
        return;
    }

    emit inplacePreviewCodeBlockUpdated(images);

    m_lastInplacePreviewSize = images.size();

    if (!blocks.isEmpty()) {
        emit checkBlocksForObsoletePreview(blocks.toList());
    }
}

void VLivePreviewHelper::mathjaxPreviewResultReady(int p_identitifer,
                                                   int p_id,
                                                   TimeStamp p_timeStamp,
                                                   const QString &p_format,
                                                   const QByteArray &p_data)
{
    if (p_identitifer != m_mathJaxID || p_timeStamp != m_timeStamp) {
        return;
    }

    if (p_id >= m_codeBlocks.size() || p_data.isEmpty()) {
        updateInplacePreview();
        return;
    }

    CodeBlockPreviewInfo &cb = m_codeBlocks[p_id];
    const QString &text = cb.codeBlock().m_text;

    QSharedPointer<CodeBlockImageCacheEntry> entry(new CodeBlockImageCacheEntry(p_timeStamp,
                                                                                p_format,
                                                                                p_data,
                                                                                getScaleFactor(cb)));
    m_cache.insert(text, entry);

    cb.updateInplacePreview(m_editor, m_doc, entry->m_image);

    updateInplacePreview();
}

void VLivePreviewHelper::clearObsoleteCache()
{
    if (m_cache.size() - m_codeBlocks.size() <= CODE_BLOCK_IMAGE_CACHE_SIZE_DIFF) {
        return;
    }

    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (m_timeStamp - it.value()->m_ts > CODE_BLOCK_IMAGE_CACHE_TIME_DIFF) {
            it.value().clear();
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}
