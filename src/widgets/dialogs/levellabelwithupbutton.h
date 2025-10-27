#ifndef LEVELLABELWITHUPBUTTON_H
#define LEVELLABELWITHUPBUTTON_H

#include <QWidget>

class QLabel;
class QPushButton;

namespace vnotex {
// Used to navigate through a series of levels.
class LevelLabelWithUpButton : public QWidget {
  Q_OBJECT
public:
  struct Level {
    QString m_name;

    const void *m_data = nullptr;
  };

  LevelLabelWithUpButton(QWidget *p_parent = nullptr);

  const Level &getLevel() const;

  // From bottom to up.
  void setLevels(const QVector<Level> &p_levels);

  void setReadOnly(bool p_readonly);

signals:
  void levelChanged();

private:
  void setupUI();

  void updateLabelAndButton();

  QLabel *m_label = nullptr;

  QPushButton *m_upButton = nullptr;

  QVector<Level> m_levels;

  int m_levelIdx = -1;

  bool m_readOnly = false;
};
} // namespace vnotex

#endif // LEVELLABELWITHUPBUTTON_H
