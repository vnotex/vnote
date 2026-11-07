#ifndef OUTLINEPROVIDER_H
#define OUTLINEPROVIDER_H

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QTextCursor>
#include <QVector>

#include <functional>
#include <limits.h>

class QTextDocument;

namespace vnotex {
enum class OutlineDropPosition { OnItem, AboveItem, BelowItem, OnViewport };

// Placement in one published outline snapshot. The insertion anchor may be outside
// the destination parent's subtree; native tree adapters then append to that parent.
struct OutlineMove {
  int m_sourceHeadingIndex = -1;
  int m_beforeHeadingIndex = -1;
  int m_targetLevel = -1;
  int m_parentHeadingIndex = -1;

  bool isValid() const { return m_sourceHeadingIndex >= 0; }
};

// Toc content.
struct Outline {
  struct Heading {
    Heading() = default;

    Heading(const QString &p_name, int p_level);

    bool operator==(const Heading &p_a) const;

    QString m_name;

    // Heading level, 1-based.
    int m_level = -1;

    bool m_isPlaceholder = false;

    // Whether this concrete heading may participate in reordering.
    bool m_reorderable = false;

    // Allows an immovable root to receive children. Movable headings also accept them.
    bool m_acceptsChildren = false;
  };

  void clear();

  bool operator==(const Outline &p_a) const;

  bool isEmpty() const;

  // In-process identity of heading order, literal names, levels and placeholders.
  // Excludes display/reordering permissions and retains no heading text.
  QByteArray navigationFingerprint() const;

  // Shared drop validation, used by both drag feedback and confirmed execution.
  OutlineMove planMove(int p_sourceHeadingIndex, int p_targetHeadingIndex,
                       OutlineDropPosition p_position) const;

  QVector<Heading> m_headings;

  // Producer guarantee: names already contain the intended section numbers.
  bool m_hasSectionNumber = false;

  // Whether this outline currently supports reordering.
  bool m_reorderSupported = false;

  // Markdown's syntax limit. Other producers explicitly opt into deeper trees.
  int m_maxLevel = 6;
};

// A command against one exact published source snapshot. Insert has no source index.
struct OutlineSourceEdit {
  enum class Kind { Move, Rename, Insert };
  Kind m_kind = Kind::Move;
  OutlineMove m_placement;
  QString m_topic;
  int m_expectedRevision = -1;
  QByteArray m_expectedFingerprint;
};

struct OutlineSourceSnapshot {
  QSharedPointer<Outline> m_outline;
  int m_revision = -1;
  // Genuine text changes only; formatting may change m_revision without changing this.
  quint64 m_contentGeneration = 0;
  QVector<int> m_headingPositions; // Published heading order; placeholders are -1.
};

// History owns receipts, not source text or an Outline. The source view temporarily
// retains the pending receipt before writing, so synchronous invalidation is observable.
struct OutlineSourceEditReceipt {
  QPointer<QTextDocument> m_document;
  int m_beforeUndoSteps = -1;
  int m_afterUndoSteps = -1;
  int m_redoSteps = -1;
  // Complete real source roster in source order, including Typst hidden boundaries.
  QVector<int> m_beforeHeadingPositions;
  QVector<int> m_afterHeadingPositions;
  QVector<int> m_beforeHeadingLevels;
  QVector<int> m_afterHeadingLevels;
  QVector<bool> m_beforeVisibleHeadings;
  QVector<bool> m_afterVisibleHeadings;
  // Preallocated before mutation; positioned after the source write, then tracked
  // through authorized joined normalization until authoritative publication settles.
  QVector<QTextCursor> m_trackedHeadingStarts;
  quint64 m_compilationEpoch = 0;
  quint64 m_compilationGeneration = 0;
  // The exact genuine-text generation acknowledged by successful settlement.
  quint64 m_contentGeneration = 0;
  bool m_awaitingBefore = false;
  bool m_pending = true;
  bool m_stale = false;
  std::function<bool()> m_undo;
  std::function<bool()> m_redo;
};

// Used to hold toc-related data of one ViewWindow.
class OutlineProvider : public QObject {
  Q_OBJECT
public:
  explicit OutlineProvider(QObject *p_parent = nullptr);

  virtual ~OutlineProvider();

  // Get the outline.
  const QSharedPointer<Outline> &getOutline() const;
  void setOutline(const QSharedPointer<Outline> &p_outline);

  void setReorderSupported(bool p_supported);

  // Per-window permission, independent of persisted outline preferences and snapshots.
  bool getAutoSectionNumberAllowed() const;
  void setAutoSectionNumberAllowed(bool p_allowed);

  void requestMove(const OutlineMove &p_move);

  // Get current heading index in outline.
  int getCurrentHeadingIndex() const;
  void setCurrentHeadingIndex(int p_idx);

  template <class T>
  static void makePerfectHeadings(const QVector<T> &p_headings, QVector<T> &p_perfectHeadings);

signals:
  void outlineChanged();

  void currentHeadingChanged();

  void autoSectionNumberAllowedChanged();

  void headingClicked(int p_idx);

  void moveRequested(const vnotex::OutlineMove &p_move);

private:
  QSharedPointer<Outline> m_outline;

  int m_currentHeadingIndex = -1;

  bool m_autoSectionNumberAllowed = true;
};

template <class T>
void OutlineProvider::makePerfectHeadings(const QVector<T> &p_headings,
                                          QVector<T> &p_perfectHeadings) {
  p_perfectHeadings.clear();
  if (p_headings.isEmpty()) {
    return;
  }

  int baseLevel = INT_MAX;
  for (const auto &heading : p_headings) {
    if (heading.m_level < baseLevel) {
      baseLevel = heading.m_level;
    }
  }

  p_perfectHeadings.reserve(p_headings.size());
  int curLevel = baseLevel - 1;
  for (const auto &heading : p_headings) {
    while (heading.m_level > curLevel + 1) {
      curLevel += 1;

      // Insert empty level which is an invalid header.
      p_perfectHeadings.append(T(tr("[EMPTY]"), curLevel));
      p_perfectHeadings.last().m_isPlaceholder = true;
    }

    p_perfectHeadings.append(heading);
    curLevel = heading.m_level;
  }
}
} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::OutlineMove)

#endif // OUTLINEPROVIDER_H
