/**
 * @file ScreenHandler.h
 * @brief Display management system for LoRa devices with OLED screens
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file defines the ScreenHandler class, which provides a simple interface
 * for managing OLED display output on LoRa devices. It handles:
 * - Multi-line text display with scrolling
 * - Status line management
 * - Screen initialization and refresh
 * - Integration with HT_SSD1306Wire display driver
 */

#ifndef SCREEN_HANDLER_H_
#define SCREEN_HANDLER_H_

#include "Arduino.h"
#include "WiFi.h"
#include "LoRaWan_APP.h"
#include <Wire.h>  
#include "HT_SSD1306Wire.h"

/**
 * @class ScreenHandler
 * @brief Simple display manager for OLED screens on LoRa devices
 * 
 * This class provides a straightforward interface for displaying text
 * on OLED screens commonly used with LoRa development boards. It manages:
 * 
 * **Display Layout:**
 * - Status line at top (persistent system information)
 * - Two scrolling text lines for messages and data
 * - Automatic screen refresh and formatting
 * 
 * **Key Features:**
 * - **Simple Interface**: Easy-to-use methods for common display tasks
 * - **Automatic Scrolling**: New lines automatically scroll older content
 * - **Status Management**: Persistent status line for system information
 * - **Hardware Abstraction**: Hides display driver complexity
 * 
 * **Display Organization:**
 * ```
 * +------------------+
 * | Status Line      |  <- Persistent status information
 * |------------------|
 * | Line 1           |  <- Scrolling message area
 * | Line 2           |  <- (older content scrolls up)
 * +------------------+
 * ```
 * 
 * **Usage Pattern:**
 * 1. Create ScreenHandler instance (or use global instance)
 * 2. Call setup() to initialize display hardware
 * 3. Use update_status() for status information
 * 4. Use add_line() to display messages and data
 * 5. Screen automatically refreshes when content changes
 * 
 * @par Example:
 * @code
 * ScreenHandler screen;
 * screen.setup();
 * screen.update_status("LoRa Ready");
 * screen.add_line("Waiting for messages...");
 * screen.add_line("Signal: -85 dBm");
 * @endcode
 */
class ScreenHandler {

    public:
        /**
         * @brief Constructor initializes the screen handler
         * 
         * Creates a new ScreenHandler instance and initializes internal
         * state for managing display content and formatting.
         */
        ScreenHandler();
        
        /**
         * @brief Add a new line to the scrolling display area
         * @param str Text content to display
         * 
         * Adds a new line of text to the scrolling display area. The content
         * will appear on the bottom line, and existing content will scroll up.
         * When the display is full, the oldest content is discarded.
         * 
         * The screen is automatically refreshed after adding the new line.
         * Text that exceeds the display width will be truncated.
         */
        void add_line(String str);
        
        /**
         * @brief Update the persistent status line
         * @param str Status text to display
         * 
         * Updates the status line at the top of the display with new information.
         * This line is persistent and doesn't scroll with regular message content.
         * Typically used for system status, connection state, or other important
         * information that should always be visible.
         * 
         * The screen is automatically refreshed after updating the status.
         */
        void update_status(String str);
        
        /**
         * @brief Initialize the display hardware and clear the screen
         * 
         * Performs initial setup of the OLED display hardware:
         * - Initializes I2C communication
         * - Configures display parameters
         * - Clears the screen
         * - Sets up initial display state
         * 
         * This method must be called before using other ScreenHandler methods.
         */
        void setup();

    private:
        String status_line;  ///< Current status line content
        String line1;        ///< First scrolling line content
        String line2;        ///< Second scrolling line content
        
        /**
         * @brief Refresh the physical display with current content
         * 
         * Updates the physical OLED display with the current text content
         * from all managed lines. Handles:
         * - Text positioning and formatting
         * - Font selection and sizing
         * - Screen clearing and refresh
         * - Hardware-specific display commands
         * 
         * This method is called automatically when content changes.
         */
        void draw_screen();
};

/**
 * @brief Global ScreenHandler instance for application use
 * 
 * Provides a global ScreenHandler instance that can be used throughout
 * the application for display management. This simplifies integration
 * and avoids the need to pass screen references between functions.
 */
extern ScreenHandler display_screen;

#endif // SCREEN_HANDLER_H_