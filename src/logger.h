#ifndef FENRIRDB_LOGGER_H
#define FENRIRDB_LOGGER_H

#include <string>
#include <iostream>

namespace FenrirDB {

class Logger {
public:
    enum class Level {
        INFO,
        WARN,
        ERROR
    };

    static Logger& get_instance() {
        static Logger instance;
        return instance;
    }

    void log(Level level, const std::string& component, const std::string& message) {
        std::string level_str;
        switch (level) {
            case Level::INFO: level_str = "[INFO]"; break;
            case Level::WARN: level_str = "[WARN]"; break;
            case Level::ERROR: level_str = "[ERROR]"; break;
        }
        std::cout << level_str << " [" << component << "] " << message << std::endl;
    }

    void info(const std::string& component, const std::string& message) { log(Level::INFO, component, message); }
    void warn(const std::string& component, const std::string& message) { log(Level::WARN, component, message); }
    void error(const std::string& component, const std::string& message) { log(Level::ERROR, component, message); }

private:
    Logger() = default;
};

} // namespace FenrirDB

#endif // FENRIRDB_LOGGER_H
