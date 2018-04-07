#include "vlivepreviewhelper.h"

#include <QDebug>

#include "veditor.h"
#include "vdocument.h"
#include "vconfigmanager.h"
#include "vgraphvizhelper.h"
#include "vplantumlhelper.h"

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

VLivePreviewHelper::VLivePreviewHelper(VEditor *p_editor,
                                       VDocument *p_document,
                                       QObject *p_parent)
    : QObject(p_parent),
      m_editor(p_editor),
      m_document(p_document),
      m_cbIndex(-1),
      m_livePreviewEnabled(false),
      m_graphvizHelper(NULL),
      m_plantUMLHelper(NULL)
{
    connect(m_editor->object(), &VEditorObject::cursorPositionChanged,
            this, &VLivePreviewHelper::handleCursorPositionChanged);

    m_flowchartEnabled = g_config->getEnableFlowchart();
    m_mermaidEnabled = g_config->getEnableMermaid();
    m_plantUMLMode = g_config->getPlantUMLMode();
    m_graphvizEnabled = g_config->getEnableGraphviz();
}

bool VLivePreviewHelper::isPreviewLang(const QString &p_lang) const
{
    return (m_flowchartEnabled && (p_lang == "flow" || p_lang == "flowchart"))
           || (m_mermaidEnabled && p_lang == "mermaid")
           || (m_plantUMLMode != PlantUMLMode::DisablePlantUML && p_lang == "puml")
           || (m_graphvizEnabled && p_lang == "dot");
}

void VLivePreviewHelper::updateCodeBlocks(const QVector<VCodeBlock> &p_codeBlocks)
{
    if (!m_livePreviewEnabled) {
        return;
    }

    int lastIndex = m_cbIndex;
    m_cbIndex = -1;
    int cursorBlock = m_editor->textCursorW().block().blockNumber();
    int idx = 0;
    bool needUpdate = true;
    int nrCached = 0;
    for (auto const & cb : p_codeBlocks) {
        if (!isPreviewLang(cb.m_lang)) {
            continue;
        }

        bool cached = false;
        if (idx < m_codeBlocks.size()) {
            CodeBlock &vcb = m_codeBlocks[idx];
            if (vcb.m_codeBlock.equalContent(cb)) {
                vcb.m_codeBlock.updateNonContent(cb);
                cached = true;
                ++nrCached;
            } else {
                vcb.m_codeBlock = cb;
                vcb.m_cachedResult.clear();
            }
        } else {
            m_codeBlocks.append(CodeBlock());
            m_codeBlocks[idx].m_codeBlock = cb;
        }

        if (cb.m_startBlock <= cursorBlock && cb.m_endBlock >= cursorBlock) {
            if (lastIndex == idx && cached) {
                needUpdate = false;
            }

            m_cbIndex = idx;
        }

        ++idx;
    }

    m_codeBlocks.resize(idx);

    qDebug() << "VLivePreviewHelper cache" << nrCached << "code blocks of" << m_codeBlocks.size();

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
        const CodeBlock &cb = m_codeBlocks[mid];

        if (cb.m_codeBlock.m_startBlock <= cursorBlock && cb.m_codeBlock.m_endBlock >= cursorBlock) {
            break;
        } else if (cb.m_codeBlock.m_startBlock > cursorBlock) {
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
    Q_ASSERT(p_text.startsWith("```") && p_text.endsWith("```"));
    int idx = p_text.indexOf('\n') + 1;
    return p_text.mid(idx, p_text.size() - idx - 3);
}

void VLivePreviewHelper::updateLivePreview()
{
    if (m_cbIndex < 0) {
        return;
    }

    Q_ASSERT(!(m_cbIndex & ~INDEX_MASK));

    const CodeBlock &cb = m_codeBlocks[m_cbIndex];
    QString text = removeFence(cb.m_codeBlock.m_text);
    qDebug() << "updateLivePreview" << m_cbIndex << cb.m_codeBlock.m_lang;

    if (cb.m_codeBlock.m_lang == "dot") {
        if (!m_graphvizHelper) {
            m_graphvizHelper = new VGraphvizHelper(this);
            connect(m_graphvizHelper, &VGraphvizHelper::resultReady,
                    this, &VLivePreviewHelper::localAsyncResultReady);
        }

        if (cb.m_cachedResult.isEmpty()) {
            m_graphvizHelper->processAsync(m_cbIndex | LANG_PREFIX_GRAPHVIZ | TYPE_LIVE_PREVIEW,
                                           "svg",
                                           text);
        } else {
            qDebug() << "use cached preview result of code block" << m_cbIndex;
            m_document->setPreviewContent(cb.m_codeBlock.m_lang, cb.m_cachedResult);
        }
    } else if (cb.m_codeBlock.m_lang == "puml" && m_plantUMLMode == PlantUMLMode::LocalPlantUML) {
        if (!m_plantUMLHelper) {
            m_plantUMLHelper = new VPlantUMLHelper(this);
            connect(m_plantUMLHelper, &VPlantUMLHelper::resultReady,
                    this, &VLivePreviewHelper::localAsyncResultReady);
        }

        if (cb.m_cachedResult.isEmpty()) {
            m_plantUMLHelper->processAsync(m_cbIndex | LANG_PREFIX_PLANTUML | TYPE_LIVE_PREVIEW,
                                           "svg",
                                           text);
        } else {
            qDebug() << "use cached preview result of code block" << m_cbIndex;
            m_document->setPreviewContent(cb.m_codeBlock.m_lang, cb.m_cachedResult);
        }
    } else {
        m_document->previewCodeBlock(m_cbIndex, cb.m_codeBlock.m_lang, text, true);
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

    if (livePreview) {
        if (idx != m_cbIndex) {
            return;
        }

        m_codeBlocks[idx].m_cachedResult = p_result;
        m_document->setPreviewContent(lang, p_result);
    }
}
