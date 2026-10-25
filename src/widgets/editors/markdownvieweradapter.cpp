#include "markdownvieweradapter.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QMap>
#include <QPointer>
#include <QStringView>
#include <QUrl>

#include <array>
#include <cmath>
#include <utility>

#include "../outlineprovider.h"
#include "graphvizhelper.h"
#include "plantumlhelper.h"
#include <core/logging.h>
#include <core/servicelocator.h>
#include <gui/utils/imageutils.h>
#include <utils/sectionnumberutils.h>
#include <vtextedit/typstmathrenderer.h>
#include <vtextedit/typstruntime.h>

using namespace vnotex;

namespace {
constexpr qsizetype c_maxTypstMathSource = 64 * 1024;
constexpr qsizetype c_maxTypstMathQueuedSource = 2 * 1024 * 1024;
constexpr qsizetype c_maxTypstMathWaiting = 32;
constexpr qsizetype c_maxTypstMathSvg = 8 * 1024 * 1024;
constexpr qsizetype c_maxTypstMathError = 64 * 1024;
constexpr qreal c_maxTypstMathDimension = 16384;

// Count without an intermediate UTF-8 allocation, rejecting ill-formed Unicode and NUL.
qsizetype typstMathUtf8Size(const QString &p_text, qsizetype p_limit) {
  qsizetype bytes = 0;
  for (qsizetype idx = 0; idx < p_text.size(); ++idx) {
    const auto ch = p_text.at(idx);
    if (ch.isNull()) {
      return -1;
    }
    if (ch.isHighSurrogate() && idx + 1 < p_text.size() && p_text.at(idx + 1).isLowSurrogate()) {
      bytes += 4;
      ++idx;
    } else if (ch.isSurrogate()) {
      return -1;
    } else {
      const auto code = ch.unicode();
      bytes += code < 0x80 ? 1 : code < 0x800 ? 2 : 3;
    }
    if (bytes > p_limit) {
      return -1;
    }
  }
  return bytes;
}

QColor typstMathForeground(const QString &p_text) {
  if (p_text.size() != 9 || p_text.at(0) != QLatin1Char('#')) {
    return {};
  }
  quint32 rgba = 0;
  for (qsizetype idx = 1; idx < p_text.size(); ++idx) {
    const auto ch = p_text.at(idx).unicode();
    int digit = -1;
    if (ch >= '0' && ch <= '9') {
      digit = ch - '0';
    } else if (ch >= 'A' && ch <= 'F') {
      digit = ch - 'A' + 10;
    } else if (ch >= 'a' && ch <= 'f') {
      digit = ch - 'a' + 10;
    }
    if (digit < 0) {
      return {};
    }
    rgba = (rgba << 4) | static_cast<quint32>(digit);
  }
  // Web RGBA is not QColor's eight-digit ARGB string format.
  return QColor((rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff, rgba & 0xff);
}

QJsonObject typstMathFailure(const QString &p_error) {
  return {{QStringLiteral("svg"), QString()},
          {QStringLiteral("widthPt"), 0},
          {QStringLiteral("heightPt"), 0},
          {QStringLiteral("baselinePt"), 0},
          {QStringLiteral("error"), p_error}};
}

QJsonObject typstMathResult(const vte::TypstMathRenderer::Result &p_result) {
  if (!p_result.isSuccess() && typstMathUtf8Size(p_result.m_error, c_maxTypstMathError) > 0) {
    return typstMathFailure(p_result.m_error);
  }
  const auto width = p_result.m_sizePt.width();
  const auto height = p_result.m_sizePt.height();
  const QByteArrayView svg(p_result.m_svg);
  if (!p_result.isSuccess() || svg.isEmpty() || svg.size() > c_maxTypstMathSvg ||
      !svg.isValidUtf8() || svg.contains('\0') || !std::isfinite(width) || !std::isfinite(height) ||
      !std::isfinite(p_result.m_baselinePt) || width <= 0 || height <= 0 ||
      width > c_maxTypstMathDimension || height > c_maxTypstMathDimension) {
    return typstMathFailure(MarkdownViewerAdapter::tr("Typst returned an invalid math result."));
  }
  return {{QStringLiteral("svg"), QString::fromUtf8(p_result.m_svg)},
          {QStringLiteral("widthPt"), width},
          {QStringLiteral("heightPt"), height},
          {QStringLiteral("baselinePt"), p_result.m_baselinePt},
          {QStringLiteral("error"), QString()}};
}
} // namespace

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

MarkdownViewerAdapter::~MarkdownViewerAdapter() {
  m_retiringTypstMath = true;
  m_typstMathActive.reset();
  m_typstMathQueue.clear();
  m_typstMathIds.clear();
  // Facade destruction suppresses callbacks and releases native work without waiting.
  delete m_typstMathRenderer;
}

void MarkdownViewerAdapter::setProtectedView(bool p_protected) {
  if (m_protectedView == p_protected) {
    return;
  }
  m_protectedView = p_protected;
  if (p_protected) {
    const bool retiring = std::exchange(m_retiringTypstMath, true);
    const QPointer<MarkdownViewerAdapter> guard(this);
    retireTypstMath();
    if (guard) {
      m_retiringTypstMath = retiring;
    }
  }
}

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

void MarkdownViewerAdapter::setRenderedHtml(QString p_generation, const QString &p_html,
                                            int p_lineNumber) {
  if (p_generation.isEmpty()) {
    return;
  }
  if (p_generation == m_generation) {
    scrollToPosition(Position(p_lineNumber, QString()));
    return;
  }
  const bool cancelContent = std::exchange(m_contentRequested, false);
  m_generation = p_generation;
  m_finishedGeneration.clear();
  m_pendingHtml = p_html;
  m_pendingLineNumber = p_lineNumber;
  m_hasPendingHtml = true;
  const auto resetGeneration = getResetGeneration();
  const QPointer<MarkdownViewerAdapter> guard(this);
  retireTypstMath(QString(), true);
  if (!guard || guard->getResetGeneration() != resetGeneration || p_generation != m_generation) {
    return;
  }
  if (cancelContent) {
    emit contentReady(QString(), QString(), QString(), QString());
    if (!guard || guard->getResetGeneration() != resetGeneration) {
      return;
    }
  }
  if (p_generation != m_generation) {
    return;
  }
  if (isReady() && !m_dispatchingHtml) {
    dispatchHtml(p_generation);
  } else if (!isReady() && !m_htmlActionPending) {
    // Bound readiness work to one action; only the latest fragment owns plaintext.
    m_htmlActionPending = true;
    pendAction([this]() {
      m_htmlActionPending = false;
      dispatchHtml(m_generation);
    });
  }
}

void MarkdownViewerAdapter::dispatchHtml(QString p_generation) {
  if (p_generation.isEmpty() || p_generation != m_generation || !isReady() || !m_hasPendingHtml ||
      m_dispatchingHtml) {
    return;
  }
  const auto html = std::exchange(m_pendingHtml, QString());
  const auto line = std::exchange(m_pendingLineNumber, -1);
  m_hasPendingHtml = false;
  m_dispatchingHtml = true;
  const auto resetGeneration = getResetGeneration();
  const QPointer<MarkdownViewerAdapter> guard(this);
  qCDebug(lcPerfPreview) << "adapter HTML dispatched generation=" << p_generation
                         << "chars=" << html.size()
                         << "atMs=" << QDateTime::currentMSecsSinceEpoch();
  emit htmlUpdated(html, p_generation);
  if (!guard) {
    return;
  }
  if (guard->getResetGeneration() == resetGeneration && p_generation == m_generation) {
    scrollToPosition(Position(line, QString()));
  }
  if (!guard) {
    return;
  }
  // Reset must not admit nested HTML while this signal is still being delivered.
  // Even after a reset, drain any fresh fragment queued by its receivers.
  m_dispatchingHtml = false;
  if (m_hasPendingHtml && isReady()) {
    const auto next = m_generation;
    QMetaObject::invokeMethod(this, [this, next]() { dispatchHtml(next); }, Qt::QueuedConnection);
  }
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

void MarkdownViewerAdapter::setWorkFinished(const QString &p_generation) {
  if (p_generation.isEmpty() || p_generation != m_generation ||
      p_generation == m_finishedGeneration) {
    return;
  }
  m_finishedGeneration = p_generation;
  emit workFinished();
}

void MarkdownViewerAdapter::setPresentationMode(bool p_active, bool p_darkTheme) {
  emit presentationModeRequested(p_active, p_darkTheme);
}

void MarkdownViewerAdapter::setPresentationState(bool p_active, const QString &p_error) {
  emit presentationStateChanged(p_active, p_error);
}

quint64 MarkdownViewerAdapter::requestImageView(const QString &p_url,
                                                const QSizeF &p_viewportHint) {
  const auto resetGeneration = getResetGeneration();
  const QPointer<MarkdownViewerAdapter> guard(this);
  cancelImageView();
  if (!guard || guard->getResetGeneration() != resetGeneration) {
    return 0;
  }
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
    const QPointer<MarkdownViewerAdapter> guard(this);
    emit imageViewCancelRequested(id);
    if (guard) {
      emit imageViewCancelled(id);
    }
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

void MarkdownViewerAdapter::saveContent() {
  if (!isDocumentReady()) {
    emit contentReady(QString(), QString(), QString(), QString());
    return;
  }
  m_contentRequested = true;
  const auto generation = m_generation;
  emit contentRequested(generation);
}

void MarkdownViewerAdapter::setSavedContent(const QString &p_generation,
                                            const QString &p_headContent,
                                            const QString &p_styleContent, const QString &p_content,
                                            const QString &p_bodyClassList) {
  if (!m_contentRequested || p_generation != m_generation) {
    return;
  }
  m_contentRequested = false;
  emit contentReady(p_headContent, p_styleContent, p_content, p_bodyClassList);
}

void MarkdownViewerAdapter::reset() {
  // Block admission through all cancellation signals, including nested resets/unprotects.
  const bool retiring = std::exchange(m_retiringTypstMath, true);
  const bool cancelContent = std::exchange(m_contentRequested, false);
  m_generation.clear();
  m_finishedGeneration.clear();
  m_pendingHtml.clear();
  m_pendingLineNumber = -1;
  m_hasPendingHtml = false;
  // Base reset clears pending actions even if the page was already unready.
  m_htmlActionPending = false;
  m_topLineNumber = -1;
  m_headings.clear();
  m_headingsHaveSectionNumber = false;
  m_currentHeadingIndex = -1;
  m_crossCopyTargets.clear();
  setReady(false);

  // Finish retiring state before cancellation receivers can publish a new document.
  const auto resetGeneration = getResetGeneration();
  const QPointer<MarkdownViewerAdapter> guard(this);
  cancelImageView();
  if (cancelContent && guard && guard->getResetGeneration() == resetGeneration) {
    emit contentReady(QString(), QString(), QString(), QString());
  }
  if (!guard) {
    return;
  }
  retireTypstMath();
  if (guard) {
    m_retiringTypstMath = retiring;
  }
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
  const auto resetGeneration = getResetGeneration();
  const auto generation = m_generation;
  const QPointer<MarkdownViewerAdapter> guard(this);
  if (p_lang == QStringLiteral("puml")) {
    PlantUmlHelper::getInst().process(
        p_id, p_index, p_format, p_text, this,
        [guard, resetGeneration, generation](quint64 id, TimeStamp timeStamp, const QString &format,
                                             const QString &data, bool success) {
          if (guard && guard->getResetGeneration() == resetGeneration &&
              guard->m_generation == generation) {
            emit guard->graphRenderDataReady(id, timeStamp, format, data, success);
          }
        },
        p_imageIndex);
  } else if (!graphvizEngine.isEmpty()) {
    GraphvizHelper::getInst().process(
        p_id, p_index, p_format, p_text, this,
        [guard, resetGeneration, generation](quint64 id, TimeStamp timeStamp, const QString &format,
                                             const QString &data, bool success) {
          if (guard && guard->getResetGeneration() == resetGeneration &&
              guard->m_generation == generation) {
            emit guard->graphRenderDataReady(id, timeStamp, format, data, success);
          }
        },
        0, graphvizEngine);
  } else {
    qWarning() << "MarkdownViewerAdapter::renderGraph unhandled lang=" << p_lang << "id=" << p_id
               << "index=" << p_index;
    Q_ASSERT(false);
  }
}

void MarkdownViewerAdapter::renderTypstMath(const QString &p_id, const QString &p_scope,
                                            const QString &p_source, bool p_display,
                                            const QString &p_foreground) {
  // A rejection with a duplicate ID would steal the original request's completion.
  if (m_typstMathIds.contains(p_id)) {
    return;
  }
  if (m_protectedView || m_retiringTypstMath) {
    emit typstMathReady(p_id, typstMathFailure(tr("Typst math is unavailable in this view.")));
    return;
  }
  const bool readerScope = p_scope.startsWith(QLatin1String("reader:")) &&
                           !m_generation.isEmpty() &&
                           QStringView(p_scope).mid(7) == QStringView(m_generation);
  const bool previewScope = p_scope.startsWith(QLatin1String("preview:")) && p_scope.size() > 8;
  if (p_id.isEmpty() || p_id.size() > 128 || p_scope.isEmpty() || p_scope.size() > 256 ||
      (!readerScope && !previewScope)) {
    emit typstMathReady(p_id,
                        typstMathFailure(tr("Invalid Typst math request identity or scope.")));
    return;
  }
  const auto foreground = typstMathForeground(p_foreground);
  if (!foreground.isValid()) {
    emit typstMathReady(p_id, typstMathFailure(tr("Invalid Typst math foreground.")));
    return;
  }
  // CRLF can occupy twice the normalized library bound; do not copy unbounded input.
  if (p_source.size() > 2 * c_maxTypstMathSource) {
    emit typstMathReady(p_id, typstMathFailure(tr("Invalid or oversized Typst math source.")));
    return;
  }
  auto source = p_source;
  source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  source.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  const auto sourceBytes = typstMathUtf8Size(source, c_maxTypstMathSource);
  if (sourceBytes <= 0 || QStringView(source).trimmed().isEmpty()) {
    emit typstMathReady(p_id, typstMathFailure(tr("Invalid or oversized Typst math source.")));
    return;
  }
  if (!m_typstMathRenderer) {
    auto *runtime = m_services ? m_services->get<vte::TypstRuntime>() : nullptr;
    if (!runtime || !runtime->isAvailable()) {
      emit typstMathReady(p_id, typstMathFailure(tr("Typst runtime is unavailable.")));
      return;
    }
    m_typstMathRenderer = new vte::TypstMathRenderer(runtime, this);
    connect(m_typstMathRenderer, &vte::TypstMathRenderer::finished, this,
            [this](const vte::TypstMathRenderer::Result &p_result) {
              m_typstMathInFlight = false;
              auto request = std::exchange(m_typstMathActive, std::nullopt);
              const QPointer<MarkdownViewerAdapter> guard(this);
              // Always drain, even if cancellation already removed the active owner.
              // Reserve its ID until publication so a nested signal cannot steal it.
              startNextTypstMath();
              if (!guard || !request) {
                return;
              }
              const bool currentReader =
                  !request->m_scope.startsWith(QLatin1String("reader:")) ||
                  QStringView(request->m_scope).mid(7) == QStringView(m_generation);
              const auto result = request->m_resetGeneration == getResetGeneration() &&
                                          currentReader && !m_protectedView && !m_retiringTypstMath
                                      ? typstMathResult(p_result)
                                      : typstMathFailure(tr("Typst math rendering was cancelled."));
              m_typstMathIds.remove(request->m_id);
              emit typstMathReady(request->m_id, result);
            });
  }
  if (m_typstMathQueue.size() >= c_maxTypstMathWaiting ||
      sourceBytes > c_maxTypstMathQueuedSource - m_typstMathQueuedBytes) {
    emit typstMathReady(p_id, typstMathFailure(tr("Typst math render queue is full.")));
    return;
  }
  TypstMathRequest request;
  request.m_id = p_id;
  request.m_scope = p_scope;
  request.m_source = std::move(source);
  request.m_foreground = foreground;
  request.m_resetGeneration = getResetGeneration();
  request.m_sourceBytes = sourceBytes;
  request.m_display = p_display;
  m_typstMathIds.insert(p_id);
  m_typstMathQueuedBytes += sourceBytes;
  m_typstMathQueue.append(std::move(request));
  startNextTypstMath();
}

void MarkdownViewerAdapter::startNextTypstMath() {
  if (m_typstMathInFlight || m_retiringTypstMath) {
    return;
  }
  while (!m_typstMathQueue.isEmpty()) {
    auto request = m_typstMathQueue.takeFirst();
    m_typstMathQueuedBytes -= request.m_sourceBytes;
    const bool staleReader = request.m_scope.startsWith(QLatin1String("reader:")) &&
                             QStringView(request.m_scope).mid(7) != QStringView(m_generation);
    if (request.m_resetGeneration != getResetGeneration() || staleReader || m_protectedView) {
      m_typstMathIds.remove(request.m_id);
      const QPointer<MarkdownViewerAdapter> guard(this);
      emit typstMathReady(request.m_id,
                          typstMathFailure(tr("Typst math rendering was cancelled.")));
      if (!guard || m_typstMathInFlight || m_retiringTypstMath) {
        return;
      }
      continue;
    }
    m_typstMathActive = std::move(request);
    m_typstMathInFlight = true;
    if (m_typstMathRenderer->renderToSvg(m_typstMathActive->m_source, m_typstMathActive->m_display,
                                         m_typstMathActive->m_foreground)) {
      // Only native work needs the source now; retain identity, not another formula copy.
      m_typstMathActive->m_source.clear();
      return;
    }
    // Busy accepts nothing. Its existing callback still owns the flight barrier,
    // never this rejected ID, and will drain any remaining queued work.
    const auto id = std::move(m_typstMathActive->m_id);
    m_typstMathActive.reset();
    m_typstMathIds.remove(id);
    emit typstMathReady(id, typstMathFailure(tr("Typst math renderer is busy.")));
    return;
  }
}

void MarkdownViewerAdapter::cancelTypstMath(const QString &p_scope) {
  if (!p_scope.isEmpty() && p_scope.size() <= 256) {
    retireTypstMath(p_scope);
  }
}

void MarkdownViewerAdapter::retireTypstMath(const QString &p_scope, bool p_readersOnly) {
  if (!m_typstMathActive && m_typstMathQueue.isEmpty()) {
    return;
  }
  const auto matches = [&p_scope, p_readersOnly](const TypstMathRequest &p_request) {
    return p_readersOnly ? p_request.m_scope.startsWith(QLatin1String("reader:"))
                         : p_scope.isEmpty() || p_request.m_scope == p_scope;
  };
  std::array<QString, c_maxTypstMathWaiting + 1> ids;
  qsizetype count = 0;
  const bool cancelActive = m_typstMathActive && matches(*m_typstMathActive);
  if (cancelActive) {
    ids[count++] = std::move(m_typstMathActive->m_id);
    m_typstMathActive.reset();
  }
  for (auto it = m_typstMathQueue.begin(); it != m_typstMathQueue.end();) {
    if (matches(*it)) {
      m_typstMathQueuedBytes -= it->m_sourceBytes;
      ids[count++] = std::move(it->m_id);
      it = m_typstMathQueue.erase(it);
    } else {
      ++it;
    }
  }
  const QPointer<MarkdownViewerAdapter> guard(this);
  if (cancelActive) {
    // Clear ownership first and cancel before signals can submit replacement work.
    // Keep m_typstMathInFlight until the facade's asynchronous terminal callback.
    m_typstMathRenderer->cancel();
  }
  const auto result = typstMathFailure(tr("Typst math rendering was cancelled."));
  for (qsizetype idx = 0; idx < count; ++idx) {
    const auto &id = ids[idx];
    m_typstMathIds.remove(id);
    emit typstMathReady(id, result);
    if (!guard) {
      return;
    }
  }
  startNextTypstMath();
}

void MarkdownViewerAdapter::onPdfRenderReady() { emit pdfRenderReady(); }

void MarkdownViewerAdapter::toggleTaskListItem(const QString &p_generation, int p_lineNumber,
                                               bool p_checked) {
  if (p_lineNumber < 0 || p_generation.isEmpty() || p_generation != m_generation) {
    emitTaskListToggleRejected(p_generation, p_lineNumber);
    return;
  }
  emit taskListItemToggleRequested(p_generation, p_lineNumber, p_checked);
}

void MarkdownViewerAdapter::emitTaskListToggleRejected(const QString &p_generation,
                                                       int p_lineNumber) {
  emit taskListToggleRejected(p_generation, p_lineNumber);
}
