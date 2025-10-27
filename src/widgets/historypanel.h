#ifndef HISTORYPANEL_H
#define HISTORYPANEL_H

#include <QDateTime>
#include <QFrame>
#include <QIcon>
#include <QScopedPointer>

#include "navigationmodewrapper.h"

class QListWidget;
class QListWidgetItem;

namespace vnotex {
class TitleBar;
struct HistoryItemFull;

class HistoryPanel : public QFrame {
  Q_OBJECT
public:
  explicit HistoryPanel(QWidget *p_parent = nullptr);

  void initialize();

private slots:
  void handleContextMenuRequested(const QPoint &p_pos);

  void openItem(const QListWidgetItem *p_item);

  void clearHistory();

private:
  struct SeparatorData {
    QString m_text;

    QDateTime m_dateUtc;
  };

  void initIcons();

  void setupUI();

  void setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

  void updateHistoryList();

  void updateSeparators();

  void addItem(const HistoryItemFull &p_hisItem);

  QString getPath(const QListWidgetItem *p_item) const;

  bool isValidItem(const QListWidgetItem *p_item) const;

  TitleBar *m_titleBar = nullptr;

  QListWidget *m_historyList = nullptr;

  QScopedPointer<NavigationModeWrapper<QListWidget, QListWidgetItem>> m_navigationWrapper;

  QVector<SeparatorData> m_separators;

  QIcon m_fileIcon;
};
} // namespace vnotex

#endif // HISTORYPANEL_H
