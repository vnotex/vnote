#ifndef ATTACHMENTDRAGDROPAREAINDICATOR2_H
#define ATTACHMENTDRAGDROPAREAINDICATOR2_H

#include <QFrame>

class QDragEnterEvent;
class QDropEvent;

namespace vnotex {

class Buffer2;

class AttachmentDragDropAreaIndicator2 : public QFrame {
  Q_OBJECT
public:
  explicit AttachmentDragDropAreaIndicator2(QWidget *p_parent = nullptr);

  void setBuffer(Buffer2 *p_buffer);

  // Check if a drag event should be accepted (has text/uri-list MIME).
  static bool isAccepted(const QDragEnterEvent *p_event);

signals:
  // Emitted after files are successfully attached.
  // @p_count: number of files attached.
  void filesAttached(int p_count);

protected:
  void dragEnterEvent(QDragEnterEvent *p_event) Q_DECL_OVERRIDE;
  void dropEvent(QDropEvent *p_event) Q_DECL_OVERRIDE;
  void mouseReleaseEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI();

  Buffer2 *m_buffer = nullptr;
};

} // namespace vnotex

#endif // ATTACHMENTDRAGDROPAREAINDICATOR2_H
