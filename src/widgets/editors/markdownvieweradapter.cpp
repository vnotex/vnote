#include "markdownvieweradapter.h"

#include <QDateTime>
#include <QMap>
#include <QUrl>

#include <utility>

#include "../outlineprovider.h"
#include "graphvizhelper.h"
#include "plantumlhelper.h"
#include <core/logging.h>
#include <core/servicelocator.h>
#include <gui/utils/imageutils.h>
#include <utils/sectionnumberutils.h>

using namespace vnotex;

MarkdownViewerAdapter::Position::Position(int p_lineNumber, const QString &p_anchor)
    : m_lineNumber(p_lineNumber), m_anchor(p_anchor) {}

QJsonObject MarkdownViewerAdapter::Position::toJson() const {
  QJsonObject obj;
  obj["lineNumber"] = m_lineNumber;
  obj["anchor"] = m_anchor;
  return obj;
}

MarkdownViewerAdapter::Heading::Heading(const QString &p_name, int p_level, const QString &p_anchor)
    : m_name(p_name), m_level(p_level), m_anchor(p_anchor) {}

MarkdownViewerAdapter::Heading MarkdownViewerAdapter::Heading::fromJson(const QJsonObject &p_obj) {
  return Heading(p_obj.value(QStringLiteral("name")).toString(),
                 p_obj.value(QStringLiteral("level")).toInt(),
                 p_obj.value(QStringLiteral("anchor")).toString());
}

MarkdownViewerAdapter::MarkdownViewerAdapter(QObject *p_parent) : WebViewAdapter(p_parent) {}

MarkdownViewerAdapter::MarkdownViewerAdapter(ServiceLocator &p_services, QObject *p_parent)
    : WebViewAdapter(p_parent), m_services(&p_services) {}

MarkdownViewerAdapter::~MarkdownViewerAdapter() {}

void MarkdownViewerAdapter::setProtectedView(bool p_protected) { m_protectedView = p_protected; }

QString MarkdownViewerAdapter::protectedImageUrl(const QString &p_url) {
  if (!m_protectedView || p_url.size() > 32 * 1024 * 1024 ||
      !p_url.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)) {
    return QString();
  }
  const int comma = p_url.indexOf(QLatin1Char(','));
  if (comma < 0) {
    return QString();
  }
  const auto header = p_url.left(comma);
  const auto encoded = p_url.mid(comma + 1).toLatin1();
  auto input = header.endsWith(QLatin1String(";base64"), Qt::CaseInsensitive)
                   ? QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors)
                   : QByteArray::fromPercentEncoding(encoded);
  QByteArray data;
  QByteArray mime;
  const bool valid = ImageUtils::protectedImageData(input, data, mime);
  input.fill(0);
  if (!valid) {
    return QString();
  }
  const auto result = QStringLiteral("data:%1;base64,%2")
                          .arg(QString::fromLatin1(mime), QString::fromLatin1(data.toBase64()));
  data.fill(0);
  return result;
}

void MarkdownViewerAdapter::activateProtectedLink(const QString &p_url) {
  if (m_protectedView && QUrl(p_url).isRelative() && !p_url.startsWith(QLatin1Char('/')) &&
      !p_url.startsWith(QLatin1Char('\\'))) {
    emit protectedLinkRequested(p_url);
  }
}

void MarkdownViewerAdapter::setText(int p_revision, const QString &p_text, int p_lineNumber) {
  if (p_revision == m_revision && p_revision != 0) {
    // Only sync line number position.
    scrollToPosition(Position(p_lineNumber, ""));
    return;
  }

  if (isReady()) {
    // Diagnostics: this is where the read-mode render pass actually starts. A
    // queued setText does not start it - the pending action only runs when the
    // viewer signals ready - so the caller's timestamp is not the pass start.
    qCDebug(lcPerfPreview) << "adapter setText dispatched revision=" << p_revision
                           << "chars=" << p_text.size()
                           << "atMs=" << QDateTime::currentMSecsSinceEpoch();
    m_revision = p_revision;
    emit textUpdated(p_text);
    scrollToPosition(Position(p_lineNumber, ""));
  } else {
    qCDebug(lcPerfPreview) << "adapter setText queued revision=" << p_revision
                           << "chars=" << p_text.size()
                           << "atMs=" << QDateTime::currentMSecsSinceEpoch();
    pendAction(std::bind(QOverload<int, const QString &, int>::of(&MarkdownViewerAdapter::setText),
                         this, p_revision, p_text, p_lineNumber));
  }
}

void MarkdownViewerAdapter::setText(const QString &p_text, int p_lineNumber) {
  setText(0, p_text, p_lineNumber);
}

void MarkdownViewerAdapter::scrollToLine(int p_lineNumber) {
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

void MarkdownViewerAdapter::setTopLineNumber(int p_lineNumber) {
  if (m_topLineNumber == p_lineNumber) {
    return;
  }

  m_topLineNumber = p_lineNumber;
}

void MarkdownViewerAdapter::scrollToPosition(const Position &p_pos) {
  if (p_pos.m_lineNumber >= 0) {
    scrollToLine(p_pos.m_lineNumber);
  } else {
    // Anchor.
    scrollToAnchor(p_pos.m_anchor);
  }
}

int MarkdownViewerAdapter::getTopLineNumber() const { return m_topLineNumber; }

void MarkdownViewerAdapter::setHeadings(const QJsonArray &p_headings, bool p_hasSectionNumber) {
  QVector<Heading> headings;
  headings.reserve(p_headings.size());
  for (auto const &arr : p_headings) {
    headings.push_back(MarkdownViewerAdapter::Heading::fromJson(arr.toObject()));
  }

  OutlineProvider::makePerfectHeadings(headings, m_headings);
  m_headingsHaveSectionNumber = !m_headings.isEmpty() && p_hasSectionNumber;
  m_currentHeadingIndex = -1;

  emit headingsChanged();
}

void MarkdownViewerAdapter::setCurrentHeadingAnchor(int p_index, const QString &p_anchor) {
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

const QVector<MarkdownViewerAdapter::Heading> &MarkdownViewerAdapter::getHeadings() const {
  return m_headings;
}

int MarkdownViewerAdapter::getCurrentHeadingIndex() const { return m_currentHeadingIndex; }

QJsonObject MarkdownViewerAdapter::getSectionNumberOptions() const {
  return {{QStringLiteral("enabled"), m_autoSectionNumberEnabled},
          {QStringLiteral("pattern"), m_sectionNumberPattern},
          {QStringLiteral("detectHeading1ForSectionNumber"), m_detectHeading1ForSectionNumber}};
}

void MarkdownViewerAdapter::setSectionNumberOptions(bool p_enabled, const QString &p_pattern,
                                                    bool p_detectHeading1ForSectionNumber) {
  const auto pattern = SectionNumberUtils::normalizePattern(p_pattern);
  if (m_autoSectionNumberEnabled == p_enabled && m_sectionNumberPattern == pattern &&
      m_detectHeading1ForSectionNumber == p_detectHeading1ForSectionNumber) {
    return;
  }
  m_autoSectionNumberEnabled = p_enabled;
  m_sectionNumberPattern = pattern;
  m_detectHeading1ForSectionNumber = p_detectHeading1ForSectionNumber;
  emit sectionNumberOptionsChanged();
}

bool MarkdownViewerAdapter::getHeadingsHaveSectionNumber() const {
  return m_headingsHaveSectionNumber;
}

void MarkdownViewerAdapter::scrollToHeading(int p_idx) {
  if (p_idx < 0 || p_idx >= m_headings.size()) {
    return;
  }

  if (m_headings[p_idx].m_anchor.isEmpty()) {
    return;
  }

  scrollToPosition(Position(-1, m_headings[p_idx].m_anchor));
}

void MarkdownViewerAdapter::scrollToAnchor(const QString &p_anchor) {
  if (p_anchor.isEmpty()) {
    return;
  }
  Q_ASSERT(isReady());
  m_currentHeadingIndex = -1;
  emit anchorScrollRequested(p_anchor);
}

void MarkdownViewerAdapter::scroll(bool p_up) { emit scrollRequested(p_up); }

void MarkdownViewerAdapter::setKeyPress(int p_key, bool p_ctrl, bool p_shift, bool p_meta) {
  emit keyPressed(p_key, p_ctrl, p_shift, p_meta);
}

void MarkdownViewerAdapter::zoom(bool p_zoomIn) { emit zoomed(p_zoomIn); }

void MarkdownViewerAdapter::setCrossCopyTargets(const QJsonArray &p_targets) {
  m_crossCopyTargets.clear();
  for (const auto &target : p_targets) {
    m_crossCopyTargets << target.toString();
  }
}

const QStringList &MarkdownViewerAdapter::getCrossCopyTargets() const { return m_crossCopyTargets; }

QString MarkdownViewerAdapter::getCrossCopyTargetDisplayName(const QString &p_target) const {
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

void MarkdownViewerAdapter::setCrossCopyResult(quint64 p_id, quint64 p_timeStamp,
                                               const QString &p_html) {
  emit crossCopyReady(p_id, p_timeStamp, p_html);
}

void MarkdownViewerAdapter::setWorkFinished() { emit workFinished(); }

void MarkdownViewerAdapter::setPresentationMode(bool p_active, bool p_darkTheme) {
  emit presentationModeRequested(p_active, p_darkTheme);
}

void MarkdownViewerAdapter::setPresentationState(bool p_active, const QString &p_error) {
  emit presentationStateChanged(p_active, p_error);
}

quint64 MarkdownViewerAdapter::requestImageView(const QString &p_url,
                                                const QSizeF &p_viewportHint) {
  cancelImageView();
  const auto id = ++m_imageViewSequence;
  m_imageViewRequest = id;
  m_pendingImageViewUrl = p_url;
  const auto dispatch = [this, id, p_viewportHint]() {
    if (id == m_imageViewRequest) {
      const auto url = std::move(m_pendingImageViewUrl);
      emit imageViewRequested(id, url, p_viewportHint.width(), p_viewportHint.height());
    }
  };
  if (isReady()) {
    dispatch();
  } else {
    // The queued closure retains only identity/geometry, not protected image bytes.
    pendAction(dispatch);
  }
  return id;
}

void MarkdownViewerAdapter::cancelImageView() {
  const auto id = m_imageViewRequest;
  m_imageViewRequest = 0;
  m_pendingImageViewUrl.clear();
  if (id) {
    emit imageViewCancelRequested(id);
    emit imageViewCancelled(id);
  }
}

void MarkdownViewerAdapter::setImageViewState(quint64 p_id, bool p_opened) {
  if (!p_id || p_id != m_imageViewRequest) {
    return;
  }
  if (!p_opened) {
    m_imageViewRequest = 0;
    m_pendingImageViewUrl.clear();
  }
  emit imageViewStateChanged(p_id, p_opened);
}

void MarkdownViewerAdapter::saveContent() { emit contentRequested(); }

void MarkdownViewerAdapter::setSavedContent(const QString &p_headContent,
                                            const QString &p_styleContent, const QString &p_content,
                                            const QString &p_bodyClassList) {
  emit contentReady(p_headContent, p_styleContent, p_content, p_bodyClassList);
}

void MarkdownViewerAdapter::reset() {
  cancelImageView();
  m_revision = 0;
  setReady(false);
  m_topLineNumber = -1;
  m_headings.clear();
  m_headingsHaveSectionNumber = false;
  m_currentHeadingIndex = -1;
  m_crossCopyTargets.clear();
}

void MarkdownViewerAdapter::renderGraph(quint64 p_id, quint64 p_index, const QString &p_format,
                                        const QString &p_lang, const QString &p_text,
                                        int p_imageIndex) {
  // Per-diagram: keep this behind a logging category. An ungated qInfo() here costs a
  // synchronous formatted write for every diagram in a document.
  qCDebug(lcUi) << "MarkdownViewerAdapter::renderGraph id=" << p_id << "index=" << p_index
                << "format=" << p_format << "lang=" << p_lang << "textLen=" << p_text.size();
  if (m_protectedView || p_text.isEmpty() || p_imageIndex < 0) {
    emit graphRenderDataReady(p_id, p_index, p_format, QString(), false);
    return;
  }

  // TODO(deferred): PlantUmlHelper/GraphvizHelper singletons will be replaced
  // by services passed through PreviewHelper pipeline in a future migration.
  // For now, both legacy and new-architecture paths use the singleton directly.
  const auto graphvizEngine = GraphvizHelper::getEngineForLanguage(p_lang);
  if (p_lang == QStringLiteral("puml")) {
    PlantUmlHelper::getInst().process(
        p_id, p_index, p_format, p_text, this,
        [this](quint64 id, TimeStamp timeStamp, const QString &format, const QString &data,
               bool success) { emit graphRenderDataReady(id, timeStamp, format, data, success); },
        p_imageIndex);
  } else if (!graphvizEngine.isEmpty()) {
    GraphvizHelper::getInst().process(
        p_id, p_index, p_format, p_text, this,
        [this](quint64 id, TimeStamp timeStamp, const QString &format, const QString &data,
               bool success) { emit graphRenderDataReady(id, timeStamp, format, data, success); },
        0, graphvizEngine);
  } else {
    qWarning() << "MarkdownViewerAdapter::renderGraph unhandled lang=" << p_lang << "id=" << p_id
               << "index=" << p_index;
    Q_ASSERT(false);
  }
}

void MarkdownViewerAdapter::onPdfRenderReady() { emit pdfRenderReady(); }

void MarkdownViewerAdapter::toggleTaskListItem(int p_lineNumber, bool p_checked) {
  if (p_lineNumber < 0) {
    return;
  }

  emit taskListItemToggleRequested(p_lineNumber, p_checked);
}

void MarkdownViewerAdapter::emitTaskListToggleRejected(int p_lineNumber) {
  emit taskListToggleRejected(p_lineNumber);
}
