#include "Logger.h"
#include <hilog/log.h>

namespace rct_io {
void Logger::platformLog(LogLevel level, const std::string& tag, const std::string& message) {
  ::LogLevel severity = LOG_INFO;
  switch (level) {
    case LogLevel::Verbose:
    case LogLevel::Debug: severity = LOG_DEBUG; break;
    case LogLevel::Info: severity = LOG_INFO; break;
    case LogLevel::Warn: severity = LOG_WARN; break;
    case LogLevel::Error: severity = LOG_ERROR; break;
  }
  OH_LOG_Print(LOG_APP, severity, 0xFF00, tag.c_str(), "%{public}s", message.c_str());
}
} // namespace rct_io
