#include "markdownvieweradapter.h"

#include <QMap>

#include "../outlineprovider.h"
#include "plantumlhelper.h"
#include "graphvizhelper.h"
#include <utils/utils.h>

using namespace vnotex;

MarkdownViewerAdapter::Position::Position(int p_lineNumber, const QString &p_anchor)
    : m_lineNumber(p_lineNumber),
      m_anchor(p_anchor)
{
}

QJsonObject MarkdownViewerAdapter::Position::toJson() const
{
    QJsonObject obj;
    obj["lineNumber"] = m_lineNumber;
    obj["anchor"] = m_anchor;
    return obj;
}

MarkdownViewerAdapter::PreviewData::PreviewData(quint64 p_id,
                                                TimeStamp p_timeStamp,
                                                const QString &p_format,
                                                const QByteArray &p_data,
                                                bool p_needScale)
    : m_id(p_id),
      m_timeStamp(p_timeStamp),
      m_format(p_format),
      m_data(p_data),
      m_needScale(p_needScale)
{
}

MarkdownViewerAdapter::Heading::Heading(const QString &p_name, int p_level, const QString &p_anchor)
    : m_name(p_name),
      m_level(p_level),
      m_anchor(p_anchor)
{
}

MarkdownViewerAdapter::Heading MarkdownViewerAdapter::Heading::fromJson(const QJsonObject &p_obj)
{
    return Heading(p_obj.value(QStringLiteral("name")).toString(),
                   p_obj.value(QStringLiteral("level")).toInt(),
                   p_obj.value(QStringLiteral("anchor")).toString());
}

MarkdownViewerAdapter::CssRuleStyle MarkdownViewerAdapter::CssRuleStyle::fromJson(const QJsonObject &p_obj)
{
    CssRuleStyle style;
    style.m_selector = p_obj[QStringLiteral("selector")].toString();
    style.m_color = p_obj[QStringLiteral("color")].toString();
    style.m_backgroundColor = p_obj[QStringLiteral("backgroundColor")].toString();
    style.m_fontWeight = p_obj[QStringLiteral("fontWeight")].toString();
    style.m_fontStyle = p_obj[QStringLiteral("fontStyle")].toString();
    return style;
}

QTextCharFormat MarkdownViewerAdapter::CssRuleStyle::toTextCharFormat() const
{
    QTextCharFormat fmt;
    if (!m_color.isEmpty()) {
        fmt.setForeground(Utils::toColor(m_color));
    }
    if (!m_backgroundColor.isEmpty()) {
        fmt.setBackground(QColor(m_color));
    }
    if (m_fontWeight.contains(QStringLiteral("bold"))) {
        fmt.setFontWeight(QFont::Bold);
    }
    if (m_fontStyle.contains(QStringLiteral("italic"))) {
        fmt.setFontItalic(true);
    }
    return fmt;
}

MarkdownViewerAdapter::MarkdownViewerAdapter(QObject *p_parent)
    : WebViewAdapter(p_parent)
{
}

MarkdownViewerAdapter::~MarkdownViewerAdapter()
{
}

void MarkdownViewerAdapter::setText(int p_revision,
                                    const QString &p_text,
                                    int p_lineNumber)
{
    if (p_revision == m_revision && p_revision != 0) {
        // Only sync line number position.
        scrollToPosition(Position(p_lineNumber, ""));
        return;
    }

    if (isReady()) {
        m_revision = p_revision;
        emit textUpdated(p_text);
        scrollToPosition(Position(p_lineNumber, ""));
    } else {
        pendAction(std::bind(QOverload<int, const QString &, int>::of(&MarkdownViewerAdapter::setText),
                             this,
                             p_revision,
                             p_text,
                             p_lineNumber));
    }
}

void MarkdownViewerAdapter::setText(const QString &p_text, int p_lineNumber)
{
    setText(0, p_text, p_lineNumber);
}

void MarkdownViewerAdapter::scrollToLine(int p_lineNumber)
{
    if (p_lineNumber == -1) {
        return;
    }

    if (isReady()) {
        m_topLineNumber = p_lineNumber;
        emit editLineNumberUpdated(p_lineNumber);
    } else {
        pendAction(std::bind(&MarkdownViewerAdapter::scrollToLine, this, p_lineNumber));
    }
}

void MarkdownViewerAdapter::setTopLineNumber(int p_lineNumber)
{
    if (m_topLineNumber == p_lineNumber) {
        return;
    }

    m_topLineNumber = p_lineNumber;
}

void MarkdownViewerAdapter::scrollToPosition(const Position &p_pos)
{
    if (p_pos.m_lineNumber >= 0) {
        scrollToLine(p_pos.m_lineNumber);
    } else {
        // Anchor.
        scrollToAnchor(p_pos.m_anchor);
    }
}

int MarkdownViewerAdapter::getTopLineNumber() const
{
    return m_topLineNumber;
}

void MarkdownViewerAdapter::setGraphPreviewData(quint64 p_id,
                                                quint64 p_timeStamp,
                                                const QString &p_format,
                                                const QString &p_data,
                                                bool p_base64,
                                                bool p_needScale)
{
    auto ba = p_data.toUtf8();
    if (p_base64 && !ba.isEmpty()) {
        ba = QByteArray::fromBase64(ba);
    }
    emit graphPreviewDataReady(PreviewData(p_id, p_timeStamp, p_format, ba, p_needScale));
}

void MarkdownViewerAdapter::setMathPreviewData(quint64 p_id,
                                               quint64 p_timeStamp,
                                               const QString &p_format,
                                               const QString &p_data,
                                               bool p_base64,
                                               bool p_needScale)
{
    auto ba = p_data.toUtf8();
    if (p_base64 && !ba.isEmpty()) {
        ba = QByteArray::fromBase64(ba);
    }
    emit mathPreviewDataReady(PreviewData(p_id, p_timeStamp, p_format, ba, p_needScale));
}

void MarkdownViewerAdapter::setHeadings(const QJsonArray &p_headings)
{
    QVector<Heading> headings;
    headings.reserve(p_headings.size());
    for (auto const &arr : p_headings) {
        headings.push_back(MarkdownViewerAdapter::Heading::fromJson(arr.toObject()));
    }

    OutlineProvider::makePerfectHeadings(headings, m_headings);
    m_currentHeadingIndex = -1;

    emit headingsChanged();
}

void MarkdownViewerAdapter::setCurrentHeadingAnchor(int p_index, const QString &p_anchor)
{
    m_currentHeadingIndex = -1;
    if (p_index > -1) {
        for (int i = p_index; i < m_headings.size(); ++i) {
            if (m_headings[i].m_anchor == p_anchor) {
                m_currentHeadingIndex = i;
                break;
            }
        }
    }

    emit currentHeadingChanged();
}

const QVector<MarkdownViewerAdapter::Heading> &MarkdownViewerAdapter::getHeadings() const
{
    return m_headings;
}

int MarkdownViewerAdapter::getCurrentHeadingIndex() const
{
    return m_currentHeadingIndex;
}

void MarkdownViewerAdapter::scrollToHeading(int p_idx)
{
    if (p_idx < 0 || p_idx >= m_headings.size()) {
        return;
    }

    if (m_headings[p_idx].m_anchor.isEmpty()) {
        return;
    }

    scrollToPosition(Position(-1, m_headings[p_idx].m_anchor));
}

void MarkdownViewerAdapter::scrollToAnchor(const QString &p_anchor)
{
    if (p_anchor.isEmpty()) {
        return;
    }
    Q_ASSERT(isReady());
    m_currentHeadingIndex = -1;
    emit anchorScrollRequested(p_anchor);
}

void MarkdownViewerAdapter::scroll(bool p_up)
{
    emit scrollRequested(p_up);
}

void MarkdownViewerAdapter::setKeyPress(int p_key, bool p_ctrl, bool p_shift, bool p_meta)
{
    emit keyPressed(p_key, p_ctrl, p_shift, p_meta);
}

void MarkdownViewerAdapter::zoom(bool p_zoomIn)
{
    emit zoomed(p_zoomIn);
}

void MarkdownViewerAdapter::setMarkdownFromHtml(quint64 p_id, quint64 p_timeStamp, const QString &p_text)
{
    emit htmlToMarkdownReady(p_id, p_timeStamp, p_text);
}

void MarkdownViewerAdapter::setCodeBlockHighlightHtml(int p_idx, quint64 p_timeStamp, const QString &p_html)
{
    emit highlightCodeBlockReady(p_idx, p_timeStamp, p_html);
}

void MarkdownViewerAdapter::setCrossCopyTargets(const QJsonArray &p_targets)
{
    m_crossCopyTargets.clear();
    for (const auto &target : p_targets) {
        m_crossCopyTargets << target.toString();
    }
}

const QStringList &MarkdownViewerAdapter::getCrossCopyTargets() const
{
    return m_crossCopyTargets;
}

QString MarkdownViewerAdapter::getCrossCopyTargetDisplayName(const QString &p_target) const
{
    static QMap<QString, QString> maps;
    if (maps.isEmpty()) {
        maps.insert("No Background", tr("No Background"));
        maps.insert("Evernote", tr("Evernote"));
        maps.insert("OneNote", tr("OneNote"));
        maps.insert("Microsoft Word", tr("Microsoft Word"));
        maps.insert("WeChat Public Account Editor", tr("WeChat Public Account Editor"));
        maps.insert("Raw HTML", tr("Raw HTML"));
    }

    auto it = maps.find(p_target);
    if (it != maps.end()) {
        return *it;
    }
    qWarning() << "missing cross copy target" << p_target;
    return p_target;
}

void MarkdownViewerAdapter::setCrossCopyResult(quint64 p_id, quint64 p_timeStamp, const QString &p_html)
{
    emit crossCopyReady(p_id, p_timeStamp, p_html);
}

void MarkdownViewerAdapter::setWorkFinished()
{
    emit workFinished();
}

void MarkdownViewerAdapter::saveContent()
{
    emit contentRequested();
}

void MarkdownViewerAdapter::setSavedContent(const QString &p_headContent,
                                            const QString &p_styleContent,
                                            const QString &p_content,
                                            const QString &p_bodyClassList)
{
    emit contentReady(p_headContent, p_styleContent, p_content, p_bodyClassList);
}

void MarkdownViewerAdapter::reset()
{
    m_revision = 0;
    setReady(false);
    m_topLineNumber = -1;
    m_headings.clear();
    m_currentHeadingIndex = -1;
    m_crossCopyTargets.clear();
}

void MarkdownViewerAdapter::renderGraph(quint64 p_id,
                                        quint64 p_index,
                                        const QString &p_format,
                                        const QString &p_lang,
                                        const QString &p_text)
{
    if (p_text.isEmpty()) {
        emit graphRenderDataReady(p_id, p_index, p_format, QString());
        return;
    }

    if (p_lang == QStringLiteral("puml")) {
        PlantUmlHelper::getInst().process(p_id,
                                          p_index,
                                          p_format,
                                          p_text,
                                          this,
                                          [this](quint64 id, TimeStamp timeStamp, const QString &format, const QString &data) {
                                              emit graphRenderDataReady(id, timeStamp, format, data);
                                          });
    } else if (p_lang == QStringLiteral("dot")) {
        GraphvizHelper::getInst().process(p_id,
                                          p_index,
                                          p_format,
                                          p_text,
                                          this,
                                          [this](quint64 id, TimeStamp timeStamp, const QString &format, const QString &data) {
                                              emit graphRenderDataReady(id, timeStamp, format, data);
                                          });
    } else {
        Q_ASSERT(false);
    }
}

void MarkdownViewerAdapter::highlightCodeBlock(int p_idx, quint64 p_timeStamp, const QString &p_text)
{
    if (isReady()) {
        emit highlightCodeBlockRequested(p_idx, p_timeStamp, p_text);
    } else {
        pendAction(std::bind(&MarkdownViewerAdapter::highlightCodeBlock, this, p_idx, p_timeStamp, p_text));
    }
}

void MarkdownViewerAdapter::setStyleSheetStyles(quint64 p_id, const QJsonArray &p_styles)
{
    QVector<CssRuleStyle> ruleStyles;
    ruleStyles.reserve(p_styles.size());
    for (int i = 0; i < p_styles.size(); ++i) {
        ruleStyles.push_back(CssRuleStyle::fromJson(p_styles[i].toObject()));
    }

    invokeCallback(p_id, &ruleStyles);
}

void MarkdownViewerAdapter::fetchStylesFromStyleSheet(const QString &p_styleSheet,
                                                      const std::function<void(const QVector<CssRuleStyle> *)> &p_callback)
{
    if (p_styleSheet.isEmpty()) {
        p_callback(nullptr);
        return;
    }

    if (isReady()) {
        const quint64 id = addCallback([p_callback](void *data) {
            p_callback(reinterpret_cast<const QVector<CssRuleStyle> *>(data));
        });
        emit parseStyleSheetRequested(id, p_styleSheet);
    } else {
        pendAction(std::bind(&MarkdownViewerAdapter::fetchStylesFromStyleSheet, this, p_styleSheet, p_callback));
    }
}
