/**
 * @file LogBundler.hpp
 * @brief Log management system for efficient data collection and transmission
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file defines the LogBundler class, which provides efficient log
 * management for applications that need to collect and transmit log data
 * over bandwidth-limited channels like LoRa. It includes:
 * - Automatic log rotation based on size limits
 * - Efficient bundling for batch transmission
 * - Memory management to prevent overflow
 * - Simple interface for log collection
 */

#include <Arduino.h>
#include <vector>

/**
 * @class LogBundler
 * @brief Efficient log collection and bundling system for resource-constrained devices
 * 
 * This class provides a memory-efficient system for collecting and managing
 * log entries on resource-constrained devices like Arduino systems. It's
 * designed for applications that need to:
 * 
 * **Key Features:**
 * - **Size-Limited Storage**: Automatically manages memory usage
 * - **Automatic Rotation**: Removes old entries when space is needed
 * - **Batch Transmission**: Bundles multiple entries for efficient transmission
 * - **FIFO Behavior**: Oldest entries are removed first when space is needed
 * 
 * **Use Cases:**
 * - Sensor data logging for periodic transmission
 * - Event logging for debugging and monitoring
 * - Status updates for remote monitoring systems
 * - Any application requiring efficient log collection
 * 
 * **Memory Management:**
 * The bundler maintains a maximum total size limit across all log entries.
 * When adding a new entry would exceed this limit, the oldest entries are
 * automatically removed until sufficient space is available.
 * 
 * @par Example:
 * @code
 * LogBundler logger(500); // 500 characters max
 * 
 * logger.addLogEntry("Sensor reading: 25.3°C");
 * logger.addLogEntry("Battery: 3.7V");
 * logger.addLogEntry("Signal: -78 dBm");
 * 
 * String allLogs = logger.bundleLogs();
 * // Transmit allLogs via LoRa
 * logger.clearLogs();
 * @endcode
 */
class LogBundler {
public:
    /**
     * @brief Constructor initializes log bundler with size limit
     * @param maxSize Maximum total size of all log entries in characters (default: 100)
     * 
     * Creates a new LogBundler with the specified size limit. The limit
     * applies to the total character count of all stored log entries,
     * not the number of entries. This helps control memory usage on
     * resource-constrained devices.
     */
    LogBundler(size_t maxSize = 100) : _maxSize(maxSize), _currentSize(0) {}

    /**
     * @brief Add a new log entry with automatic size management
     * @param logEntry String containing the log entry to add
     * 
     * Adds a new log entry to the collection, automatically managing storage:
     * 1. Calculates the size needed for the new entry
     * 2. Removes oldest entries if necessary to make space
     * 3. Adds the new entry to the collection
     * 4. Updates the total size counter
     * 
     * If the new entry is larger than the total available space, older
     * entries will be removed until sufficient space is available. If
     * the single entry exceeds the total size limit, only that entry
     * will be stored.
     */
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

    /**
     * @brief Bundle all log entries into a single string for transmission
     * @return String containing all log entries separated by newlines
     * 
     * Combines all currently stored log entries into a single string
     * suitable for transmission or storage. Entries are separated by
     * newline characters for easy parsing on the receiving end.
     * 
     * The resulting string can be transmitted via LoRa, stored to a file,
     * or processed by other systems. The original log entries remain
     * stored until explicitly cleared.
     */
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

    /**
     * @brief Clear all stored log entries and reset size counter
     * 
     * Removes all stored log entries and resets the internal size counter
     * to zero. This is typically called after successfully transmitting
     * or processing the bundled logs to prepare for the next collection
     * cycle.
     * 
     * After calling this method, the bundler is ready to accept new
     * log entries up to the configured size limit.
     */
    void clearLogs() {
        _logs.clear();
        _currentSize = 0;
    }

    /**
     * @brief Get the current number of stored log entries
     * @return Number of log entries currently stored
     */
    size_t getLogCount() const {
        return _logs.size();
    }

    /**
     * @brief Get the current total size of stored log entries
     * @return Total character count of all stored entries
     */
    size_t getCurrentSize() const {
        return _currentSize;
    }

    /**
     * @brief Get the maximum allowed size for log storage
     * @return Maximum total size limit in characters
     */
    size_t getMaxSize() const {
        return _maxSize;
    }

private:
    std::vector<String> _logs;  ///< Container for stored log entries
    size_t _maxSize;            ///< Maximum allowed size for all logs combined
    size_t _currentSize;        ///< Current total size of all log entries
};