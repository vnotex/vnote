#ifndef IMAGESIZEDIALOG_H
#define IMAGESIZEDIALOG_H

#include "dialog.h"

class QLineEdit;

namespace vnotex {

// Set (or clear) the declared pixel size of the image under the cursor.
//
// Both fields are optional; leaving BOTH empty means "no size", which is what
// converts a sized HTML `<img>` back to a Markdown link where that is lossless.
// Follows the shape of its sibling ImageInsertDialog (no `2` suffix, no
// ServiceLocator) rather than the *Dialog2 controller pattern.
class ImageSizeDialog : public Dialog {
  Q_OBJECT
public:
  ImageSizeDialog(const QString &p_title, int p_width, int p_height, QWidget *p_parent = nullptr);

  // 0 means "unspecified" for that axis.
  int getImageWidth() const;

  int getImageHeight() const;

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI(int p_width, int p_height);

  QLineEdit *m_widthEdit = nullptr;

  QLineEdit *m_heightEdit = nullptr;
};

} // namespace vnotex

#endif // IMAGESIZEDIALOG_H
