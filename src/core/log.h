// FrontierDog.log next to the .asi; the previous session is kept as FrontierDog.log.old.
#pragma once
#include <string>

namespace fd::log {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void init(const std::string& path, Level minLevel);
void setLevel(Level minLevel);
void write(Level level, const char* fmt, ...);

}  // namespace fd::log

#define FD_DEBUG(...) ::fd::log::write(::fd::log::Level::Debug, __VA_ARGS__)
#define FD_INFO(...) ::fd::log::write(::fd::log::Level::Info, __VA_ARGS__)
#define FD_WARN(...) ::fd::log::write(::fd::log::Level::Warn, __VA_ARGS__)
#define FD_ERROR(...) ::fd::log::write(::fd::log::Level::Error, __VA_ARGS__)
