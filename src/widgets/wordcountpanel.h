#ifndef WORDCOUNTPANEL_H
#define WORDCOUNTPANEL_H

#include <QWidget>

class QLabel;

namespace vnotex {

struct WordCountInfo {
  bool m_isSelection = false;
  int m_wordCount = 0;
  int m_charWithoutSpaceCount = 0;
  int m_charWithSpaceCount = 0;
};

class WordCountPanel : public QWidget {
  Q_OBJECT
public:
  explicit WordCountPanel(QWidget *p_parent = nullptr);

  void updateCount(bool p_isSelection, int p_words, int p_charsWithoutSpace, int p_charsWithSpace);

  // Calculate word count statistics for the given text.
  // Counts words (English word groups + individual CJK characters),
  // chars without spaces, and total chars.
  static WordCountInfo calculateWordCount(const QString &p_text);

private:
  QLabel *m_selectionLabel = nullptr;
  QLabel *m_wordLabel = nullptr;
  QLabel *m_charWithoutSpaceLabel = nullptr;
  QLabel *m_charWithSpaceLabel = nullptr;
};

} // namespace vnotex

#endif // WORDCOUNTPANEL_H
