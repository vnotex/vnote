#ifndef VNOTEX_TODOSTICKER_H
#define VNOTEX_TODOSTICKER_H

#include "sticker.h"

#include <QDate>
#include <QPointer>

class QComboBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QShowEvent;
class QToolButton;
class QTimer;

namespace vnotex {

class HookManager;
class TodoStickerController;
class TodoTaskView;

class TodoSticker : public Sticker {
  Q_OBJECT

public:
  explicit TodoSticker(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~TodoSticker() override;

  QString typeId() const override;
  QString titleText() const override;

public slots:
  void setDueDate(QDate p_date);

protected:
  void showEvent(QShowEvent *p_event) override;
  void hideEvent(QHideEvent *p_event) override;

private:
  void updateState();
  void refreshIcons();
  void chooseFile();
  void submitTask();

  TodoStickerController *m_controller = nullptr;
  QPointer<HookManager> m_hookManager;
  QLineEdit *m_addTask = nullptr;
  QComboBox *m_lists = nullptr;
  TodoTaskView *m_tasks = nullptr;
  QToolButton *m_edit = nullptr;
  QToolButton *m_settings = nullptr;
  QLabel *m_message = nullptr;
  QTimer *m_dayTimer = nullptr;
  QString m_addTaskText;
  int m_addTaskToken = 0;
  int m_dateChangedHookId = -1;
  int m_dateClickedHookId = -1;
  bool m_initialized = false;
};

} // namespace vnotex

#endif // VNOTEX_TODOSTICKER_H
