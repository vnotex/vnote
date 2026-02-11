#ifndef SERVICELOCATOR_H
#define SERVICELOCATOR_H

#include <typeindex>
#include <unordered_map>

#include "noncopyable.h"

namespace vnotex {

// ServiceLocator provides a simple dependency injection registry.
// It stores non-owning pointers to services. The caller is responsible
// for managing service lifetime (services must outlive the ServiceLocator).
class ServiceLocator : private Noncopyable {
public:
  ServiceLocator() = default;
  ~ServiceLocator() = default;

  // Register a service. Overwrites if already registered.
  // @p_service: Non-owning pointer. Caller must ensure lifetime.
  template <typename T> void registerService(T *p_service) {
    m_services[std::type_index(typeid(T))] = static_cast<void *>(p_service);
  }

  // Retrieve a service by type.
  // @return: Pointer to service, or nullptr if not registered.
  template <typename T> T *get() const {
    auto it = m_services.find(std::type_index(typeid(T)));
    if (it != m_services.end()) {
      return static_cast<T *>(it->second);
    }
    return nullptr;
  }

  // Check if a service is registered.
  template <typename T> bool has() const {
    return m_services.find(std::type_index(typeid(T))) != m_services.end();
  }

private:
  std::unordered_map<std::type_index, void *> m_services;
};

} // namespace vnotex

#endif // SERVICELOCATOR_H
