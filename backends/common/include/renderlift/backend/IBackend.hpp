#pragma once
#include "renderlift/Types.hpp"
#include <string_view>

namespace renderlift::backend {
struct BackendStatus {
  bool initialized{};
  bool hooksReady{};
  std::string_view message{"not initialized"};
};
class IBackend {
public:
  virtual ~IBackend()=default;
  virtual GraphicsApi api() const=0;
  virtual std::string_view name() const=0;
  virtual BackendStatus status() const=0;
  virtual bool initialize()=0;
  virtual void shutdown()=0;
};
}
