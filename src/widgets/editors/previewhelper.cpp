#include "previewhelper.h"

#include <QDebug>
#include <QTextDocument>
#include <QTextBlock>
#include <QTimer>

#include <vtextedit/texteditorconfig.h>
#include <vtextedit/previewmgr.h>
#include <vtextedit/textutils.h>

#include <utils/utils.h>

#include "markdowneditor.h"
#include "plantumlhelper.h"
#include "graphvizhelper.h"

using namespace vnotex;

PreviewHelper::CodeBlockPreviewData::CodeBlockPreviewData(const vte::peg::FencedCodeBlock &p_codeBlock)
    : m_startBlock(p_codeBlock.m_startBlock),
      m_endBlock(p_codeBlock.m_endBlock),
      m_lang(p_codeBlock.m_lang)
{
}

void PreviewHelper::CodeBlockPreviewData::updateInplacePreview(QTextDocument *p_doc,
                                                               const QPixmap &p_image,
                                                               const QString &p_imageName,
                                                               QRgb p_background,
                                                               int p_tabStopWidth)
{
    const auto block = p_doc->findBlockByNumber(m_endBlock);
    if (block.isValid()) {
        m_inplacePreview.reset(new vte::PreviewItem());
        m_inplacePreview->m_startPos = block.position();
        m_inplacePreview->m_endPos = m_inplacePreview->m_startPos + block.length();
        m_inplacePreview->m_blockPos = m_inplacePreview->m_startPos;
        m_inplacePreview->m_blockNumber = m_endBlock;
        m_inplacePreview->m_padding = vte::PreviewMgr::calculateBlockMargin(block, p_tabStopWidth);
        m_inplacePreview->m_name = p_imageName;
        m_inplacePreview->m_backgroundColor = p_background;
        m_inplacePreview->m_isBlockwise = true;
        m_inplacePreview->m_image = p_image;
    } else {
        m_inplacePreview.clear();
    }
}

PreviewHelper::MathBlockPreviewData::MathBlockPreviewData(const vte::peg::MathBlock &p_mathBlock)
    : m_blockNumber(p_mathBlock.m_blockNumber),
      m_previewedAsBlock(p_mathBlock.m_previewedAsBlock),
      m_index(p_mathBlock.m_index),
      m_length(p_mathBlock.m_length)
{
}

void PreviewHelper::MathBlockPreviewData::updateInplacePreview(QTextDocument *p_doc,
                                                               const QPixmap &p_image,
                                                               const QString &p_imageName,
                                                               int p_tabStopWidth)
{
    const auto block = p_doc->findBlockByNumber(m_blockNumber);
    if (block.isValid()) {
        m_inplacePreview.reset(new vte::PreviewItem());
        m_inplacePreview->m_startPos = block.position() + m_index;
        m_inplacePreview->m_endPos = m_inplacePreview->m_startPos + m_length;
        m_inplacePreview->m_blockPos = block.position();
        m_inplacePreview->m_blockNumber = m_blockNumber;
        m_inplacePreview->m_padding = vte::PreviewMgr::calculateBlockMargin(block, p_tabStopWidth);
        m_inplacePreview->m_name = p_imageName;
        m_inplacePreview->m_isBlockwise = m_previewedAsBlock;
        m_inplacePreview->m_image = p_image;
    } else {
        m_inplacePreview.clear();
    }
}

int PreviewHelper::GraphPreviewData::s_imageIndex = 0;

PreviewHelper::GraphPreviewData::GraphPreviewData(TimeStamp p_timeStamp,
                                                  const QString &p_format,
                                                  const QByteArray &p_data,
                                                  QRgb p_background,
                                                  qreal p_scaleFactor)
    : m_timeStamp(p_timeStamp),
      m_background(p_background)
{
    if (p_data.isEmpty()) {
        return;
    }

    m_name = QString::number(++s_imageIndex);

    bool needScale = p_scaleFactor > 1.01;
    if (needScale) {
        if (p_format == QStringLiteral("svg")) {
            m_image = Utils::svgToPixmap(p_data, p_background, p_scaleFactor);
        } else {
            QPixmap tmpImg;
            tmpImg.loadFromData(p_data, p_format.toLocal8Bit().data());
            m_image = tmpImg.scaledToWidth(tmpImg.width() * p_scaleFactor, Qt::SmoothTransformation);
        }
    } else {
        m_image.loadFromData(p_data, p_format.toLocal8Bit().data());
    }
}

bool PreviewHelper::GraphPreviewData::isNull() const
{
    return m_timeStamp == 0;
}

PreviewHelper::PreviewHelper(MarkdownEditor *p_editor, QObject *p_parent)
    : QObject(p_parent),
      m_inplacePreviewSources(SourceFlag::FlowChart
                              | SourceFlag::Mermaid
                              | SourceFlag::WaveDrom
                              | SourceFlag::PlantUml
                              | SourceFlag::Graphviz
                              | SourceFlag::Math),
      m_codeBlockCache(100, nullptr),
      m_mathBlockCache(100, nullptr)
{
    setMarkdownEditor(p_editor);

    const int interval = 1000;
    m_codeBlockTimer = new QTimer(this);
    m_codeBlockTimer->setSingleShot(true);
    m_codeBlockTimer->setInterval(interval);
    connect(m_codeBlockTimer, &QTimer::timeout,
            this, &PreviewHelper::handleCodeBlocksUpdate);

    m_mathBlockTimer = new QTimer(this);
    m_mathBlockTimer->setSingleShot(true);
    m_mathBlockTimer->setInterval(interval);
    connect(m_mathBlockTimer, &QTimer::timeout,
            this, &PreviewHelper::handleMathBlocksUpdate);
}

void PreviewHelper::codeBlocksUpdated(vte::TimeStamp p_timeStamp,
                                      const QVector<vte::peg::FencedCodeBlock> &p_codeBlocks)
{
    Q_UNUSED(p_timeStamp);
    if (!m_inplacePreviewCodeBlocksEnabled) {
        return;
    }

    m_pendingCodeBlocks = p_codeBlocks;
    m_codeBlockTimer->start();
}

void PreviewHelper::handleCodeBlocksUpdate()
{
    ++m_codeBlockTimeStamp;
    m_codeBlocksData.clear();

    QVector<int> needPreviewBlocks;

    for (int i = 0; i < m_pendingCodeBlocks.size(); ++i) {
        const auto &cb = m_pendingCodeBlocks[i];

        const auto needPreview = isLangNeedPreview(cb.m_lang);
        if (!needPreview.first && !needPreview.second) {
            continue;
        }

        m_codeBlocksData.append(CodeBlockPreviewData(cb));
        const int blockPreviewIdx = m_codeBlocksData.size() - 1;

        bool cacheHit = false;
        auto &cachedData = m_codeBlockCache.get(cb.m_text);
        if (cachedData) {
            cacheHit = true;
            cachedData->m_timeStamp = m_codeBlockTimeStamp;
            m_codeBlocksData[blockPreviewIdx].updateInplacePreview(m_document,
                                                                   cachedData->m_image,
                                                                   cachedData->m_name,
                                                                   cachedData->m_background,
                                                                   m_tabStopWidth);
        }

        if (m_inplacePreviewCodeBlocksEnabled && needPreview.first && !cacheHit) {
            m_codeBlocksData[blockPreviewIdx].m_text = cb.m_text;
            needPreviewBlocks.push_back(blockPreviewIdx);
        }
    }

    for (auto idx : needPreviewBlocks) {
        inplacePreviewCodeBlock(idx);
    }

    updateEditorInplacePreviewCodeBlock();

    m_pendingCodeBlocks.clear();
}

bool PreviewHelper::checkPreviewSourceLang(SourceFlag p_flag, const QString &p_lang) const
{
    switch (p_flag) {
    case SourceFlag::FlowChart:
        return p_lang == QStringLiteral("flow") || p_lang == QStringLiteral("flowchart");

    case SourceFlag::WaveDrom:
        return p_lang == QStringLiteral("wavedrom");

    case SourceFlag::Mermaid:
        return p_lang == QStringLiteral("mermaid");

    case SourceFlag::PlantUml:
        return p_lang == QStringLiteral("puml") || p_lang == QStringLiteral("plantuml");

    case SourceFlag::Graphviz:
        return p_lang == QStringLiteral("dot") || p_lang == QStringLiteral("graphviz");

    case SourceFlag::Math:
        return p_lang == QStringLiteral("mathjax");
    }

    return false;
}

QPair<bool, bool> PreviewHelper::isLangNeedPreview(const QString &p_lang) const
{
    QPair<bool, bool> res(false, false);
    if ((isInplacePreviewSourceEnabled(SourceFlag::FlowChart) && checkPreviewSourceLang(SourceFlag::FlowChart, p_lang))
        || (isInplacePreviewSourceEnabled(SourceFlag::WaveDrom) && checkPreviewSourceLang(SourceFlag::WaveDrom, p_lang))
        || (isInplacePreviewSourceEnabled(SourceFlag::Mermaid) && checkPreviewSourceLang(SourceFlag::Mermaid, p_lang))
        || (isInplacePreviewSourceEnabled(SourceFlag::PlantUml) && checkPreviewSourceLang(SourceFlag::PlantUml, p_lang))
        || (isInplacePreviewSourceEnabled(SourceFlag::Graphviz) && checkPreviewSourceLang(SourceFlag::Graphviz, p_lang))
        || (isInplacePreviewSourceEnabled(SourceFlag::Math) && checkPreviewSourceLang(SourceFlag::Math, p_lang))) {
        res.first = res.second = true;
    }
    return res;
}

bool PreviewHelper::isInplacePreviewSourceEnabled(SourceFlag p_flag) const
{
    return m_inplacePreviewSources & p_flag;
}

void PreviewHelper::inplacePreviewCodeBlock(int p_blockPreviewIdx)
{
    const auto &blockData = m_codeBlocksData[p_blockPreviewIdx];
    Q_ASSERT(!blockData.m_text.isEmpty());
    if (checkPreviewSourceLang(SourceFlag::FlowChart, blockData.m_lang)
        || checkPreviewSourceLang(SourceFlag::WaveDrom, blockData.m_lang)
        || checkPreviewSourceLang(SourceFlag::Mermaid, blockData.m_lang)
        || (checkPreviewSourceLang(SourceFlag::PlantUml, blockData.m_lang) && m_webPlantUmlEnabled)
        || (checkPreviewSourceLang(SourceFlag::Graphviz, blockData.m_lang) && m_webGraphvizEnabled)
        || checkPreviewSourceLang(SourceFlag::Math, blockData.m_lang)) {
        emit graphPreviewRequested(p_blockPreviewIdx,
                                   m_codeBlockTimeStamp,
                                   blockData.m_lang,
                                   vte::TextUtils::removeCodeBlockFence(blockData.m_text));
        return;
    }

    if (!m_webPlantUmlEnabled && checkPreviewSourceLang(SourceFlag::PlantUml, blockData.m_lang)) {
        // Local PlantUml.
        PlantUmlHelper::getInst().process(static_cast<quint64>(p_blockPreviewIdx),
                                          m_codeBlockTimeStamp,
                                          QStringLiteral("svg"),
                                          vte::TextUtils::removeCodeBlockFence(blockData.m_text),
                                          this,
                                          [this](quint64 id, TimeStamp timeStamp, const QString &format, const QString &data) {
                                              handleLocalData(id, timeStamp, format, data, true);
                                          });
        return;
    }

    if (!m_webGraphvizEnabled && checkPreviewSourceLang(SourceFlag::Graphviz, blockData.m_lang)) {
        // Local PlantUml.
        GraphvizHelper::getInst().process(static_cast<quint64>(p_blockPreviewIdx),
                                          m_codeBlockTimeStamp,
                                          QStringLiteral("svg"),
                                          vte::TextUtils::removeCodeBlockFence(blockData.m_text),
                                          this,
                                          [this](quint64 id, TimeStamp timeStamp, const QString &format, const QString &data) {
                                              handleLocalData(id, timeStamp, format, data, false);
                                          });
        return;
    }
}

void PreviewHelper::handleGraphPreviewData(const MarkdownViewerAdapter::PreviewData &p_data)
{
    if (p_data.m_timeStamp != m_codeBlockTimeStamp) {
        return;
    }
    if (p_data.m_id >= static_cast<quint64>(m_codeBlocksData.size()) || p_data.m_data.isEmpty()) {
        updateEditorInplacePreviewCodeBlock();
        return;
    }

    auto &blockData = m_codeBlocksData[p_data.m_id];
    const bool forcedBackground = needForcedBackground(blockData.m_lang);
    auto previewData = QSharedPointer<GraphPreviewData>::create(p_data.m_timeStamp,
                                                                p_data.m_format,
                                                                p_data.m_data,
                                                                forcedBackground ? m_editor->getPreviewBackground() : 0,
                                                                p_data.m_needScale ? getEditorScaleFactor() : 1);
    m_codeBlockCache.set(blockData.m_text, previewData);
    blockData.m_text.clear();

    blockData.updateInplacePreview(m_document,
                                   previewData->m_image,
                                   previewData->m_name,
                                   previewData->m_background,
                                   m_tabStopWidth);

    updateEditorInplacePreviewCodeBlock();
}

void PreviewHelper::updateEditorInplacePreviewCodeBlock()
{
    QSet<int> obsoleteBlocks;
    QVector<QSharedPointer<vte::PreviewItem>> previewItems;
    previewItems.reserve(m_codeBlocksData.size());
    for (const auto &blockData : m_codeBlocksData) {
        if (blockData.m_inplacePreview) {
            if (!blockData.m_inplacePreview->m_image.isNull()) {
                previewItems.append(blockData.m_inplacePreview);
            } else {
                obsoleteBlocks.insert(blockData.m_inplacePreview->m_blockNumber);
            }
        } else {
            obsoleteBlocks.insert(blockData.m_endBlock);
        }
    }

    if (previewItems.isEmpty() && m_previousInplacePreviewCodeBlockSize == 0) {
        return;
    }

    emit inplacePreviewCodeBlockUpdated(previewItems);

    m_previousInplacePreviewCodeBlockSize = previewItems.size();

    if (!obsoleteBlocks.isEmpty()) {
        emit potentialObsoletePreviewBlocksUpdated(obsoleteBlocks.values());
    }

    m_codeBlockCache.setCapacityHint(m_codeBlocksData.size());
}

void PreviewHelper::setMarkdownEditor(MarkdownEditor *p_editor)
{
    Q_ASSERT(!m_editor);
    m_editor = p_editor;
    if (m_editor) {
        m_document = m_editor->document();
        m_tabStopWidth = m_editor->getConfig().m_tabStopWidth;
    }
}

void PreviewHelper::mathBlocksUpdated(const QVector<vte::peg::MathBlock> &p_mathBlocks)
{
    if (!m_inplacePreviewMathBlocksEnabled || !isInplacePreviewSourceEnabled(SourceFlag::Math)) {
        return;
    }

    m_pendingMathBlocks = p_mathBlocks;
    m_mathBlockTimer->start();
}

void PreviewHelper::handleMathBlocksUpdate()
{
    ++m_mathBlockTimeStamp;
    m_mathBlocksData.clear();
    m_mathBlocksData.reserve(m_pendingMathBlocks.size());

    bool needUpdateEditorInplacePreview = true;

    for (const auto &mb : m_pendingMathBlocks) {
        m_mathBlocksData.append(MathBlockPreviewData(mb));
        const int blockPreviewIdx = m_mathBlocksData.size() - 1;

        bool cacheHit = false;
        auto &cachedData = m_mathBlockCache.get(mb.m_text);
        if (cachedData) {
            cacheHit = true;
            cachedData->m_timeStamp = m_mathBlockTimeStamp;
            m_mathBlocksData[blockPreviewIdx].updateInplacePreview(m_document,
                                                                   cachedData->m_image,
                                                                   cachedData->m_name,
                                                                   m_tabStopWidth);
        }

        if (!cacheHit) {
            needUpdateEditorInplacePreview = false;
            m_mathBlocksData[blockPreviewIdx].m_text = mb.m_text;
            inplacePreviewMathBlock(blockPreviewIdx);
        }
    }

    if (needUpdateEditorInplacePreview) {
        updateEditorInplacePreviewMathBlock();
    }

    m_pendingMathBlocks.clear();
}

void PreviewHelper::inplacePreviewMathBlock(int p_blockPreviewIdx)
{
    const auto &blockData = m_mathBlocksData[p_blockPreviewIdx];
    Q_ASSERT(!blockData.m_text.isEmpty());
    emit mathPreviewRequested(p_blockPreviewIdx, m_mathBlockTimeStamp, blockData.m_text);
}

void PreviewHelper::updateEditorInplacePreviewMathBlock()
{
    QSet<int> obsoleteBlocks;
    QVector<QSharedPointer<vte::PreviewItem>> previewItems;
    previewItems.reserve(m_mathBlocksData.size());
    for (const auto &blockData : m_mathBlocksData) {
        if (blockData.m_inplacePreview) {
            if (!blockData.m_inplacePreview->m_image.isNull()) {
                previewItems.append(blockData.m_inplacePreview);
            } else {
                obsoleteBlocks.insert(blockData.m_inplacePreview->m_blockNumber);
            }
        } else {
            obsoleteBlocks.insert(blockData.m_blockNumber);
        }
    }

    if (previewItems.isEmpty() && m_previousInplacePreviewMathBlockSize == 0) {
        return;
    }

    emit inplacePreviewMathBlockUpdated(previewItems);

    m_previousInplacePreviewMathBlockSize = previewItems.size();

    if (!obsoleteBlocks.isEmpty()) {
        emit potentialObsoletePreviewBlocksUpdated(obsoleteBlocks.values());
    }

    m_mathBlockCache.setCapacityHint(m_mathBlocksData.size());
}

void PreviewHelper::handleMathPreviewData(const MarkdownViewerAdapter::PreviewData &p_data)
{
    if (p_data.m_timeStamp != m_mathBlockTimeStamp) {
        return;
    }
    if (p_data.m_id >= static_cast<quint64>(m_mathBlocksData.size()) || p_data.m_data.isEmpty()) {
        updateEditorInplacePreviewMathBlock();
        return;
    }

    auto &blockData = m_mathBlocksData[p_data.m_id];
    auto previewData = QSharedPointer<GraphPreviewData>::create(p_data.m_timeStamp,
                                                                p_data.m_format,
                                                                p_data.m_data,
                                                                0,
                                                                p_data.m_needScale ? getEditorScaleFactor() : 1);
    m_mathBlockCache.set(blockData.m_text, previewData);
    blockData.m_text.clear();

    blockData.updateInplacePreview(m_document,
                                   previewData->m_image,
                                   previewData->m_name,
                                   m_tabStopWidth);

    updateEditorInplacePreviewMathBlock();
}

qreal PreviewHelper::getEditorScaleFactor() const
{
    if (m_editor) {
        return m_editor->getConfig().m_scaleFactor;
    }

    return 1;
}

void PreviewHelper::setWebPlantUmlEnabled(bool p_enabled)
{
    m_webPlantUmlEnabled = p_enabled;
}

void PreviewHelper::setWebGraphvizEnabled(bool p_enabled)
{
    m_webGraphvizEnabled = p_enabled;
}

void PreviewHelper::handleLocalData(quint64 p_id,
                                    TimeStamp p_timeStamp,
                                    const QString &p_format,
                                    const QString &p_data,
                                    bool p_forcedBackground)
{
    if (p_timeStamp != m_codeBlockTimeStamp) {
        return;
    }

    Q_UNUSED(p_format);
    Q_ASSERT(p_format == QStringLiteral("svg"));

    if (p_id >= static_cast<quint64>(m_codeBlocksData.size()) || p_data.isEmpty()) {
        updateEditorInplacePreviewCodeBlock();
        return;
    }

    auto &blockData = m_codeBlocksData[p_id];
    auto previewData = QSharedPointer<GraphPreviewData>::create(p_timeStamp,
                                                                p_format,
                                                                p_data.toUtf8(),
                                                                p_forcedBackground ? m_editor->getPreviewBackground() : 0,
                                                                getEditorScaleFactor());
    m_codeBlockCache.set(blockData.m_text, previewData);
    blockData.m_text.clear();

    blockData.updateInplacePreview(m_document,
                                   previewData->m_image,
                                   previewData->m_name,
                                   previewData->m_background,
                                   m_tabStopWidth);

    updateEditorInplacePreviewCodeBlock();
}

bool PreviewHelper::needForcedBackground(const QString &p_lang) const
{
    if (checkPreviewSourceLang(SourceFlag::PlantUml, p_lang)) {
        return true;
    }

    return false;
}

void PreviewHelper::setInplacePreviewSources(SourceFlags p_srcs)
{
    m_inplacePreviewSources = p_srcs;
}

void PreviewHelper::setInplacePreviewCodeBlocksEnabled(bool p_enabled)
{
    m_inplacePreviewCodeBlocksEnabled = p_enabled;
}

void PreviewHelper::setInplacePreviewMathBlocksEnabled(bool p_enabled)
{
    m_inplacePreviewMathBlocksEnabled = p_enabled;
}
