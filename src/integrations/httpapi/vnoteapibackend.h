#ifndef VNOTEX_VNOTEAPIBACKEND_H
#define VNOTEX_VNOTEAPIBACKEND_H

#include <QObject>
#include <api/iapibackend.h>
#include <memory>

namespace vnotex {
class ServiceLocator;

// GUI-owner adapter. Workers retain only owned snapshot inputs, never services or this object.
class VNoteApiBackend final : public QObject, public IApiBackend {
  Q_OBJECT
public:
  explicit VNoteApiBackend(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~VNoteApiBackend() override;
  void execute(ApiCommand p_command, ApiRequestContext p_context, Completion p_complete) override;
  void stop() override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace vnotex
#endif
