#include "vlivepreviewhelper.h"

#include <QDebug>

#include "veditor.h"
#include "vdocument.h"
#include "vconfigmanager.h"
#include "vgraphvizhelper.h"
#include "vplantumlhelper.h"
#include "vcodeblockhighlighthelper.h"

extern VConfigManager *g_config;

// Use the highest 4 bits (31-28) to indicate the lang.
#define LANG_PREFIX_GRAPHVIZ 0x10000000UL
#define LANG_PREFIX_PLANTUML 0x20000000UL
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

void CodeBlockPreviewInfo::clearImageData()
{
    m_imgData.clear();
    m_inplacePreview.clear();
}

void CodeBlockPreviewInfo::updateNonContent(const QTextDocument *p_doc,
                                            const VCodeBlock &p_cb)
{
    m_codeBlock.updateNonContent(p_cb);
    if (m_inplacePreview.isNull()) {
        return;
    }

    QTextBlock block = p_doc->findBlockByNumber(m_codeBlock.m_endBlock);
    if (block.isValid()) {
        m_inplacePreview->m_startPos = block.position();
        m_inplacePreview->m_endPos = block.position() + block.length();
        m_inplacePreview->m_blockPos = block.position();
        m_inplacePreview->m_blockNumber = m_codeBlock.m_endBlock;
    } else {
        m_inplacePreview->clear();
    }
}

// Update inplace preview according to m_imgData.
void CodeBlockPreviewInfo::updateInplacePreview(const VEditor *p_editor,
                                                const QTextDocument *p_doc)
{
    QTextBlock block = p_doc->findBlockByNumber(m_codeBlock.m_endBlock);
    if (block.isValid()) {
        if (m_inplacePreview.isNull()) {
            m_inplacePreview.reset(new VImageToPreview());
        }

        // m_image will be generated when signaling out.
        m_inplacePreview->m_startPos = block.position();
        m_inplacePreview->m_endPos = block.position() + block.length();
        m_inplacePreview->m_blockPos = block.position();
        m_inplacePreview->m_blockNumber = m_codeBlock.m_endBlock;
        m_inplacePreview->m_padding = VPreviewManager::calculateBlockMargin(block,
                                                                            p_editor->tabStopWidthW());
        m_inplacePreview->m_name = QString::number(getImageIndex());
        m_inplacePreview->m_isBlock = true;
    } else {
        m_inplacePreview->clear();
    }
}


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
      m_plantUMLHelper(NULL)
{
    connect(m_editor->object(), &VEditorObject::cursorPositionChanged,
            this, &VLivePreviewHelper::handleCursorPositionChanged);
    connect(m_document, &VDocument::codeBlockPreviewReady,
            this, &VLivePreviewHelper::webAsyncResultReady);

    m_flowchartEnabled = g_config->getEnableFlowchart();
    m_mermaidEnabled = g_config->getEnableMermaid();
    m_plantUMLMode = g_config->getPlantUMLMode();
    m_graphvizEnabled = g_config->getEnableGraphviz();
    m_mathjaxEnabled = g_config->getEnableMathjax();
}

bool VLivePreviewHelper::isPreviewLang(const QString &p_lang) const
{
    return (m_flowchartEnabled && (p_lang == "flow" || p_lang == "flowchart"))
           || (m_mermaidEnabled && p_lang == "mermaid")
           || (m_plantUMLMode != PlantUMLMode::DisablePlantUML && p_lang == "puml")
           || (m_graphvizEnabled && p_lang == "dot")
           || (m_mathjaxEnabled && p_lang == "mathjax");
}

void VLivePreviewHelper::updateCodeBlocks(const QVector<VCodeBlock> &p_codeBlocks)
{
    if (!m_livePreviewEnabled && !m_inplacePreviewEnabled) {
        return;
    }

    int lastIndex = m_cbIndex;
    m_cbIndex = -1;
    int cursorBlock = m_editor->textCursorW().block().blockNumber();
    int idx = 0;
    bool needUpdate = true;
    for (auto const & vcb : p_codeBlocks) {
        if (!isPreviewLang(vcb.m_lang)) {
            continue;
        }

        bool cached = false;
        if (idx < m_codeBlocks.size()) {
            CodeBlockPreviewInfo &cb = m_codeBlocks[idx];
            if (cb.codeBlock().equalContent(vcb)) {
                cb.updateNonContent(m_doc, vcb);
                cached = true;
            } else {
                cb.setCodeBlock(vcb);
            }
        } else {
            m_codeBlocks.append(CodeBlockPreviewInfo(vcb));
        }

        if (m_inplacePreviewEnabled
            && !m_codeBlocks[idx].inplacePreviewReady()) {
            processForInplacePreview(idx);
        }

        if (m_livePreviewEnabled
            && vcb.m_startBlock <= cursorBlock
            && vcb.m_endBlock >= cursorBlock) {
            if (lastIndex == idx && cached) {
                needUpdate = false;
            }

            m_cbIndex = idx;
        }

        ++idx;
    }

    m_codeBlocks.resize(idx);

    if (m_livePreviewEnabled && needUpdate) {
        updateLivePreview();
    }
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
                                           "svg",
                                           removeFence(vcb.m_text));
        } else {
            m_document->setPreviewContent(vcb.m_lang, cb.imageData());
        }
    } else if (vcb.m_lang != "puml") {
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
        m_codeBlocks.clear();
        m_document->previewCodeBlock(-1, "", "", true);
    }
}

void VLivePreviewHelper::setInplacePreviewEnabled(bool p_enabled)
{
    if (m_inplacePreviewEnabled == p_enabled) {
        return;
    }

    m_inplacePreviewEnabled = p_enabled;
    if (!m_livePreviewEnabled) {
        for (auto & cb : m_codeBlocks) {
            cb.clearImageData();
        }
    }
}

void VLivePreviewHelper::localAsyncResultReady(int p_id,
                                               const QString &p_format,
                                               const QString &p_result)
{
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
    cb.setImageData(p_format, p_result);
    if (livePreview) {
        if (idx != m_cbIndex) {
            return;
        }

        m_document->setPreviewContent(lang, p_result);
    } else {
        // Inplace preview.
        cb.updateInplacePreview(m_editor, m_doc);

        updateInplacePreview();
    }
}

void VLivePreviewHelper::processForInplacePreview(int p_idx)
{
    CodeBlockPreviewInfo &cb = m_codeBlocks[p_idx];
    const VCodeBlock &vcb = cb.codeBlock();
    if (vcb.m_lang == "dot") {
        if (!m_graphvizHelper) {
            m_graphvizHelper = new VGraphvizHelper(this);
            connect(m_graphvizHelper, &VGraphvizHelper::resultReady,
                    this, &VLivePreviewHelper::localAsyncResultReady);
        }

        if (cb.hasImageData()) {
            cb.updateInplacePreview(m_editor, m_doc);
            updateInplacePreview();
        } else {
            m_graphvizHelper->processAsync(p_idx | LANG_PREFIX_GRAPHVIZ | TYPE_INPLACE_PREVIEW,
                                           "svg",
                                           removeFence(vcb.m_text));
        }
    } else if (vcb.m_lang == "puml" && m_plantUMLMode == PlantUMLMode::LocalPlantUML) {
        if (!m_plantUMLHelper) {
            m_plantUMLHelper = new VPlantUMLHelper(this);
            connect(m_plantUMLHelper, &VPlantUMLHelper::resultReady,
                    this, &VLivePreviewHelper::localAsyncResultReady);
        }

        if (cb.hasImageData()) {
            cb.updateInplacePreview(m_editor, m_doc);
            updateInplacePreview();
        } else {
            m_plantUMLHelper->processAsync(p_idx | LANG_PREFIX_PLANTUML | TYPE_INPLACE_PREVIEW,
                                           "svg",
                                           removeFence(vcb.m_text));
        }
    } else if (vcb.m_lang == "flow"
               || vcb.m_lang == "flowchart") {
        m_document->previewCodeBlock(p_idx,
                                     vcb.m_lang,
                                     removeFence(vcb.m_text),
                                     false);
    }
}

void VLivePreviewHelper::updateInplacePreview()
{
    QVector<QSharedPointer<VImageToPreview> > images;
    for (int i = 0; i < m_codeBlocks.size(); ++i) {
        CodeBlockPreviewInfo &cb = m_codeBlocks[i];
        if (cb.inplacePreviewReady() && cb.hasImageData()) {
            Q_ASSERT(!cb.inplacePreview().isNull());
            // Generate the image.
            cb.inplacePreview()->m_image.loadFromData(cb.imageData().toUtf8(),
                                                      cb.imageFormat().toLocal8Bit().data());
            images.append(cb.inplacePreview());
        }
    }

    emit inplacePreviewCodeBlockUpdated(images);

    // Clear image.
    for (int i = 0; i < m_codeBlocks.size(); ++i) {
        CodeBlockPreviewInfo &cb = m_codeBlocks[i];
        if (cb.inplacePreviewReady() && cb.hasImageData()) {
            cb.inplacePreview()->m_image = QPixmap();
        }
    }
}

void VLivePreviewHelper::webAsyncResultReady(int p_id,
                                             const QString &p_lang,
                                             const QString &p_result)
{
    Q_UNUSED(p_lang);
    if (p_id >= m_codeBlocks.size() || p_result.isEmpty()) {
        return;
    }

    CodeBlockPreviewInfo &cb = m_codeBlocks[p_id];
    cb.setImageData(QStringLiteral("svg"), p_result);
    cb.updateInplacePreview(m_editor, m_doc);
    updateInplacePreview();
}
