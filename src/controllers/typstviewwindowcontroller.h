#ifndef TYPSTVIEWWINDOWCONTROLLER_H
#define TYPSTVIEWWINDOWCONTROLLER_H

#include "textviewwindowcontroller.h"

#include <QImage>
#include <QSet>
#include <QUrl>

#include <core/fileopensettings.h>
#include <core/services/buffer2.h>

namespace vnotex {

class TypstViewWindowController : public TextViewWindowController {
  Q_OBJECT
public:
  explicit TypstViewWindowController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  struct Project {
    QString root;
    QString mainFile;
    bool memoryOnly = false;
  };

  bool setBuffer(const Buffer2 &p_buffer, QString *p_error = nullptr);
  const Project &getProject() const;

  // Per-binding ownership: only opening literal references and committed image inserts.
  // Finalization belongs to accepted final-view close/replacement, never save or Undo.
  // Both current and saved source must prove absence; uncertainty retains every asset.
  void snapshotInitialImages(const QString &p_source);
  void trackInsertedImage(const QString &p_assetIdentifier);
  void clearObsoleteImages();

  void requestOpenSource(const QString &p_file, int p_start, int p_end);
  // Apply caller routing only after resolving an allowed local file; external consent
  // and internal source-location navigation retain their existing policy.
  void requestOpenLink(const QUrl &p_url, const FileOpenSettings &p_settings = FileOpenSettings());
  void openConfirmedExternalLink(const QUrl &p_url);

  // Read-only context actions decode the compiler's captured bytes, never a live-file
  // replacement. File addresses are resolved separately and stay inside the project.
  QImage decodeImageForViewing(const QByteArray &p_data, QString *p_error = nullptr) const;
  QString imageResourcePath(const QString &p_projectPath, QString *p_error = nullptr) const;

  // Cancel insertion work independently of pending external-link consent.
  void invalidateInsertionRequests();

  // Images are stored as local files inside the current note-directory project.
  // Acquisition validates bounded image bytes and normalizes unsupported native formats.
  QByteArray readImageData(const QString &p_source, QString *p_error = nullptr);
  QString insertImage(const QByteArray &p_data, const QString &p_name, QString *p_createdAsset,
                      QString *p_error = nullptr);
  // No-copy references return decoded project-relative paths and transfer no ownership.
  QString imageReference(const QUrl &p_url, QString *p_error = nullptr) const;

  // Hyperlinks, including attachments, may target existing files outside the project.
  // These APIs never open links or request external-link consent.
  QString linkDestination(const QString &p_source, bool p_relative,
                          QString *p_error = nullptr) const;
  bool canAttach(const QUrl &p_url) const;
  QString insertAttachment(const QString &p_path, QString *p_createdAttachment,
                           QString *p_error = nullptr);
  bool readFileContent(const QUrl &p_url, QString *p_text, QString *p_error = nullptr) const;

  // Error/output parameters are cleared on entry. On successful storage, created outputs
  // contain backend identifiers owned by the caller until its insertion commits. A failed
  // insertion rolls back its own resource and reports any cleanup failure explicitly.
  // Serializers produce markup snippets with literal strings, not context-aware rewrites.
  static QString imageMarkup(const QString &p_path, const QString &p_alt, int p_width,
                             int p_height);
  static QString linkMarkup(const QString &p_destination, const QString &p_text);

signals:
  void errorOccurred(const QString &p_message);
  void externalLinkConfirmationRequested(const QUrl &p_url);

private:
  bool bufferAvailable() const;
  bool resourceAvailable() const;
  bool insertionAvailable() const;
  bool insertionIsCurrent(quint64 p_generation, const QString &p_bufferId,
                          const QString &p_resolvedPath) const;
  static QString quoteString(const QString &p_text);
  NodeIdentifier resolveIndexedFile(const QString &p_absolutePath) const;
  void confirmExternalLink(const QUrl &p_url);

  ServiceLocator &m_services;
  Buffer2 m_buffer;
  Project m_project;
  QString m_initialImageSource;
  bool m_initialImagesSnapshotted = false;
  QSet<QString> m_insertedImages;
  quint64 m_generation = 0;
  quint64 m_insertionGeneration = 0;
  QString m_insertionBufferPath;
  QUrl m_pendingExternalLink;
  quint64 m_pendingExternalGeneration = 0;
  bool m_externalConfirmationActive = false;
};

} // namespace vnotex

#endif // TYPSTVIEWWINDOWCONTROLLER_H
