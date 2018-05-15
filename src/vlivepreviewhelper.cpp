#include "vlivepreviewhelper.h"

#include <QDebug>
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

#define SCALE_FACTOR_THRESHOLD 1.1

CodeBlockPreviewInfo::CodeBlockPreviewInfo()
{
}

CodeBlockPreviewInfo::CodeBlockPreviewInfo(const VCodeBlock &p_cb)
    : m_codeBlock(p_cb)
{
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
        // Padding is not changed since content is not changed.
    } else {
        m_inplacePreview.clear();
    }
}

void CodeBlockPreviewInfo::updateInplacePreview(const VEditor *p_editor,
                                                const QTextDocument *p_doc,
                                                qreal p_scaleFactor)
{
    QTextBlock block = p_doc->findBlockByNumber(m_codeBlock.m_endBlock);
    if (block.isValid()) {
        Qt::TransformationMode tMode = Qt::SmoothTransformation;
        VImageToPreview *preview = new VImageToPreview();

        preview->m_startPos = block.position();
        preview->m_endPos = block.position() + block.length();
        preview->m_blockPos = block.position();
        preview->m_blockNumber = m_codeBlock.m_endBlock;
        preview->m_padding = VPreviewManager::calculateBlockMargin(block,
                                                                   p_editor->tabStopWidthW());
        preview->m_name = QString::number(getImageIndex());
        preview->m_isBlock = true;

        if (hasImageData()) {
            if (p_scaleFactor < SCALE_FACTOR_THRESHOLD) {
                preview->m_image.loadFromData(m_imgData.toUtf8(),
                                              m_imgFormat.toLocal8Bit().data());
            } else {
                QPixmap tmpImg;
                tmpImg.loadFromData(m_imgData.toUtf8(),
                                    m_imgFormat.toLocal8Bit().data());
                preview->m_image = tmpImg.scaledToWidth(tmpImg.width() * p_scaleFactor, tMode);
            }
        } else if (hasImageDataBa()) {
            if (p_scaleFactor < SCALE_FACTOR_THRESHOLD) {
                preview->m_image.loadFromData(m_imgDataBa,
                                              m_imgFormat.toLocal8Bit().data());
            } else {
                QPixmap tmpImg;
                tmpImg.loadFromData(m_imgDataBa,
                                    m_imgFormat.toLocal8Bit().data());
                preview->m_image = tmpImg.scaledToWidth(tmpImg.width() * p_scaleFactor, tMode);
            }
        } else {
            preview->m_image = QPixmap();
        }

        m_inplacePreview.reset(preview);
    } else {
        m_inplacePreview.clear();
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

    ++m_timeStamp;

    int lastIndex = m_cbIndex;
    m_cbIndex = -1;
    int cursorBlock = m_editor->textCursorW().block().blockNumber();
    int idx = 0;
    bool needUpdate = m_livePreviewEnabled;
    bool manualInplacePreview = m_inplacePreviewEnabled;
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
            && (!cached || !m_codeBlocks[idx].inplacePreviewReady())) {
            processForInplacePreview(idx);
            manualInplacePreview = false;
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

    if (idx == m_codeBlocks.size()) {
        manualInplacePreview = false;
    } else {
        m_codeBlocks.resize(idx);
    }

    if (manualInplacePreview) {
        updateInplacePreview();
    }

    if (needUpdate) {
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
    cb.setImageData(p_format, p_result);
    if (livePreview) {
        if (idx != m_cbIndex) {
            return;
        }

        m_document->setPreviewContent(lang, p_result);
    } else {
        // Inplace preview.
        cb.updateInplacePreview(m_editor, m_doc, getScaleFactor(cb));

        updateInplacePreview();
    }
}

void VLivePreviewHelper::processForInplacePreview(int p_idx)
{
    CodeBlockPreviewInfo &cb = m_codeBlocks[p_idx];
    const VCodeBlock &vcb = cb.codeBlock();
    Q_ASSERT(!(cb.hasImageData() || cb.hasImageDataBa()));
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
    cb.setImageDataBa(p_format, p_data);
    cb.updateInplacePreview(m_editor, m_doc, getScaleFactor(cb));
    updateInplacePreview();
}
