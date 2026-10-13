#ifndef MINDMAPEDITOR_H
#define MINDMAPEDITOR_H

#include <m3/qt/editor.h>

#include <QPointer>

class QDialog;
class QJsonArray;
class QJsonObject;

#include <core/services/buffer2.h>

namespace vnotex {
class MindMapViewWindowController;
class ServiceLocator;

class MindMapEditor : public m3::qt::MindMapEditor {
  Q_OBJECT
public:
  MindMapEditor(ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent = nullptr);

  bool loadContent(const QByteArray &p_content);
  // Commits the active draft; throws std::runtime_error rather than returning stale content.
  QString contentForSave();
  void setBuffer(const Buffer2 &p_buffer);
  void applyConfig();
  void setModified(bool p_modified);
  bool isModified() const;
  bool canInsert() const;
  void typeImage();
  void typeLink();

signals:
  void contentsChanged();
  void insertionAvailabilityChanged();
  void statusMessageRequested(const QString &p_message);

protected:
  bool canInsertFromMimeData(const QMimeData *p_source,
                             MimeInsertionSource p_origin) const override;
  void insertFromMimeData(const QString &p_nodeId, const QMimeData *p_source,
                          MimeInsertionSource p_origin) override;
  void changeEvent(QEvent *p_event) override;
  void onAddUrl(const QString &p_nodeId) override;
  void onAddImage(const QString &p_nodeId) override;

private slots:
  void onProtectedLockingChanged(bool p_locking);

private:
  enum class InsertionKind {
    Image,
    Base64,
    ImageLink,
    RelativeImageLink,
    Link,
    RelativeLink,
    Attachment,
    Text,
    FileContent,
    ImageFromUrl,
    ImageFromData
  };
  struct Insertion;
  struct MimeSnapshot;
  static MimeSnapshot snapshotMimeData(const QMimeData *p_source);
  std::unique_ptr<Insertion> beginInsertion(const QString &p_nodeId);
  bool insertionCurrent(const Insertion &p_insertion) const;
  void invalidateInsertion();
  int execInsertionDialog(Insertion &p_insertion, QDialog *p_dialog);
  bool prepareImage(Insertion &p_insertion, const QString &p_source, const QImage &p_pixels,
                    int p_storage, QJsonObject &p_record);
  bool prepareLink(Insertion &p_insertion, const QString &p_url, bool p_allowEmpty,
                   QJsonObject &p_record);
  void applyInsertion(Insertion &p_insertion, const QJsonArray &p_records);
  bool canUseBuffer() const;
  bool canEdit() const;
  void markModified();
  void invalidateContent(const QString &p_error);

  ServiceLocator &m_services;
  Buffer2 m_buffer;
  // Owned by this widget; all resource and link policy stays in the controller.
  MindMapViewWindowController *m_controller = nullptr;
  quint64 m_generation = 0;
  quint64 m_insertionSerial = 0;
  QPointer<QDialog> m_insertionDialog;
  QString m_loadError;
  bool m_loading = false;
  bool m_contentLoaded = false;
  bool m_modified = false;
};
} // namespace vnotex

#endif // MINDMAPEDITOR_H
