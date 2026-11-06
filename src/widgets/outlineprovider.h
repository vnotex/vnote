#ifndef OUTLINEPROVIDER_H
#define OUTLINEPROVIDER_H

#include <QByteArray>
#include <QObject>
#include <QSharedPointer>
#include <QVector>

#include <limits.h>

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
