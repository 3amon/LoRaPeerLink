#include <Arduino.h>
#include <vector>

class LogBundler {
public:
    LogBundler(size_t maxSize = 100) : _maxSize(maxSize), _currentSize(0) {}

    // Adds a new log entry, keeping total log size under maxSize
    void addLogEntry(const String& logEntry) {
        size_t entrySize = logEntry.length();

        // Remove oldest entries until there's enough space for the new entry
        while (_currentSize + entrySize > _maxSize && !_logs.empty()) {
            _currentSize -= _logs.front().length();
            _logs.erase(_logs.begin());
        }

        // Add the new entry and update the current size
        _logs.push_back(logEntry);
        _currentSize += entrySize;
    }

    // Bundles all log entries into a single string for transmission
    String bundleLogs() const {
        String bundledLogs;
        for (size_t i = 0; i < _logs.size(); i++) {
            bundledLogs += _logs[i];
            if (i < _logs.size() - 1) {
                bundledLogs += "\n";  // Add newline except after the last entry
            }
        }
        return bundledLogs;
    }

    // Clears all stored logs
    void clearLogs() {
        _logs.clear();
        _currentSize = 0;
    }

private:
    std::vector<String> _logs;
    size_t _maxSize;       // Maximum allowed size for all logs combined
    size_t _currentSize;   // Current total size of all log entries
};