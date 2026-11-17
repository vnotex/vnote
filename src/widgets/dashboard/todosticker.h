#ifndef VNOTEX_TODOSTICKER_H
#define VNOTEX_TODOSTICKER_H

#include "sticker.h"

class QComboBox;
class QLabel;
class QListView;
class QShowEvent;
class QToolButton;

namespace vnotex {

class TodoStickerController;

class TodoSticker : public Sticker {
  Q_OBJECT

public:
  explicit TodoSticker(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString typeId() const override;
  QString titleText() const override;

protected:
  void showEvent(QShowEvent *p_event) override;

private:
  void updateState();
  void chooseFile();

  TodoStickerController *m_controller = nullptr;
  QComboBox *m_lists = nullptr;
  QListView *m_tasks = nullptr;
  QToolButton *m_edit = nullptr;
  QToolButton *m_settings = nullptr;
  QLabel *m_message = nullptr;
  bool m_initialized = false;
};

} // namespace vnotex

#endif // VNOTEX_TODOSTICKER_H
