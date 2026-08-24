#ifndef WEBENGINEPROFILESERVICE_H
#define WEBENGINEPROFILESERVICE_H

#include <QObject>
#include <QString>

#include "core/noncopyable.h"

class QWebEngineProfile;

namespace vnotex {

// Owns a named (non off-the-record) QtWebEngine profile with a disk HTTP cache, so that
// remote resources (mainly images) survive tab close and application restart.
//
// Lifetime: the profile must outlive every QWebEnginePage created with it. Declare this
// service before the main window so that destruction order tears the window down first.
class WebEngineProfileService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit WebEngineProfileService(const QString &p_cacheRoot, QObject *p_parent = nullptr);

  QWebEngineProfile *profile() const;

  // Path derivation helpers (free of Qt WebEngine, testable).
  static QString webCachePath(const QString &p_root);

  static QString webStoragePath(const QString &p_root);

private:
  QWebEngineProfile *m_profile = nullptr;
};

} // namespace vnotex

#endif // WEBENGINEPROFILESERVICE_H
