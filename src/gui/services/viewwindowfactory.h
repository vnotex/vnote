#ifndef VIEWWINDOWFACTORY_H
#define VIEWWINDOWFACTORY_H

#include <QObject>
#include <QString>
#include <QHash>
#include <functional>

#include <core/global.h>
#include <core/noncopyable.h>

class QWidget;

namespace vnotex {

class ServiceLocator;
class Buffer2;
class ViewWindow2;

class ViewWindowFactory : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Creator function type.
  // Takes ServiceLocator, Buffer2 handle, parent widget, and initial view mode.
  // Returns a heap-allocated ViewWindow2 subclass (caller takes ownership).
  using CreatorFunc = std::function<ViewWindow2 *(ServiceLocator &, const Buffer2 &, QWidget *,
                                                  ViewWindowMode)>;

  explicit ViewWindowFactory(QObject *p_parent = nullptr);
  ~ViewWindowFactory() override;

  // Register all built-in file type creators (text, markdown, etc.).
  // Called once during startup to populate the factory.
  void registerBuiltInCreators();

  // Register a creator for a file type (e.g., "markdown", "text", "pdf").
  // Overwrites any existing creator for that type.
  void registerCreator(const QString &p_fileType, CreatorFunc p_creator);

  // Unregister a creator for a file type.
  void unregisterCreator(const QString &p_fileType);

  // Check if a creator exists for the given file type.
  bool hasCreator(const QString &p_fileType) const;

  // Create a ViewWindow2 for the given file type.
  // @p_mode: initial view mode for the window (e.g., Read or Edit).
  // Returns nullptr if no creator is registered for the type.
  ViewWindow2 *create(const QString &p_fileType, ServiceLocator &p_services,
                      const Buffer2 &p_buffer, QWidget *p_parent = nullptr,
                      ViewWindowMode p_mode = ViewWindowMode::Read) const;

private:
  QHash<QString, CreatorFunc> m_creators;
};

} // namespace vnotex

#endif // VIEWWINDOWFACTORY_H
