#ifndef OUTLINEPROVIDER_H
#define OUTLINEPROVIDER_H

#include <QObject>
#include <QSharedPointer>
#include <QVector>

#include <limits.h>

namespace vnotex {
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
  };

  void clear();

  bool operator==(const Outline &p_a) const;

  bool isEmpty() const;

  QVector<Heading> m_headings;

  // Producer guarantee: names already contain the intended section numbers.
  bool m_hasSectionNumber = false;

  // Whether this outline currently supports reordering.
  bool m_reorderSupported = false;
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

  void requestMove(int p_sourceHeadingIndex, int p_beforeHeadingIndex, int p_targetLevel);

  // Get current heading index in outline.
  int getCurrentHeadingIndex() const;
  void setCurrentHeadingIndex(int p_idx);

  template <class T>
  static void makePerfectHeadings(const QVector<T> &p_headings, QVector<T> &p_perfectHeadings);

signals:
  void outlineChanged();

  void currentHeadingChanged();

  void headingClicked(int p_idx);

  void moveRequested(int p_sourceHeadingIndex, int p_beforeHeadingIndex, int p_targetLevel);

private:
  QSharedPointer<Outline> m_outline;

  int m_currentHeadingIndex = -1;
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

#endif // OUTLINEPROVIDER_H
