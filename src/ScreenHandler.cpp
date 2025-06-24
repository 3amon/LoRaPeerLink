/**
 * @file ScreenHandler.cpp
 * @brief Implementation of OLED display management for LoRa devices
 * @author LoRaPeerLink Project
 * @version 1.0
 * 
 * This file implements the ScreenHandler class, providing simple and efficient
 * management of OLED displays on LoRa development boards. It handles display
 * initialization, text rendering, scrolling, and power management for
 * common OLED configurations.
 */

#include <ScreenHandler.h>

/**
 * @brief Forward declarations for power management functions
 * 
 * These functions control the external power supply (Vext) for the OLED display,
 * allowing proper power sequencing and energy management.
 */
void VextON(void);
void VextOFF(void);

/**
 * @brief Static OLED display object for hardware interface
 * 
 * Configured for typical LoRa board OLED setup:
 * - I2C address: 0x3c
 * - Clock speed: 500 kHz
 * - Geometry: 64x32 pixels
 * - Uses board-specific pin definitions for SDA, SCL, and reset
 */
static SSD1306Wire factory_display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_64_32, RST_OLED);

/**
 * @brief Global ScreenHandler instance for application use
 * 
 * Provides a global instance that can be accessed throughout the application
 * for consistent display management without requiring object passing.
 */
ScreenHandler display_screen = ScreenHandler();

/**
 * @brief Constructor initializes display content with default values
 * 
 * Sets up the initial state of the display handler with default content:
 * - Status line shows "Boot" to indicate initialization
 * - Message lines are empty and ready for content
 */
ScreenHandler::ScreenHandler()
{
    status_line = "Boot";
    line1 = "";
    line2 = "";
}

/**
 * @brief Render current content to the physical display
 * 
 * Updates the physical OLED display with the current text content:
 * - Draws status line at the top (y=0)
 * - Draws first message line (y=10)
 * - Draws second message line (y=20)
 * - Refreshes the display and clears the buffer for next frame
 * 
 * This method is called automatically when content changes to ensure
 * the display stays synchronized with the internal state.
 */
void ScreenHandler::draw_screen()
{
    factory_display.drawString(0, 0, status_line);   // Top line - status
    factory_display.drawString(0, 10, line1);        // Middle line - older message
    factory_display.drawString(0, 20, line2);        // Bottom line - newest message
    factory_display.display();                       // Refresh physical display
    factory_display.clear();                         // Clear buffer for next frame
}

/**
 * @brief Update the persistent status line and refresh display
 * @param str New status text to display
 * 
 * Updates the status line at the top of the display with new information.
 * This line is persistent and doesn't scroll with regular messages.
 * Typically used for system status, connection state, or other important
 * information that should always be visible.
 */
void ScreenHandler::update_status(String str)
{
    status_line = str;
    draw_screen();
}

/**
 * @brief Add a new line to the scrolling message area
 * @param str Text content to add as a new line
 * 
 * Adds a new line of text to the scrolling message area:
 * - Current line2 content moves to line1 (scrolls up)
 * - New content becomes line2 (appears at bottom)
 * - Oldest content (previous line1) is discarded
 * - Display is automatically refreshed
 * 
 * This creates a simple two-line scrolling display for messages and data.
 */
void ScreenHandler::add_line(String str)
{
    line1 = line2;   // Move current bottom line to middle
    line2 = str;     // New content goes to bottom line
    draw_screen();   // Refresh display with new content
}

/**
 * @brief Initialize the OLED display hardware and prepare for use
 * 
 * Performs complete initialization of the display system:
 * 1. Configures LED indicator (turns off)
 * 2. Enables display power supply (Vext)
 * 3. Initializes OLED controller
 * 4. Clears display and shows initial content
 * 5. Allows settling time for stable operation
 * 
 * This method must be called before using other display functions.
 */
void ScreenHandler::setup()
{
    // Configure and turn off status LED
    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);

    // Enable display power supply
    VextON();
    
    // Initialize OLED controller
    factory_display.init();
    factory_display.clear();
    
    // Display initial content
    draw_screen();
    
    // Allow time for display to stabilize
    delay(500);
}

/**
 * @brief Enable external power supply for OLED display
 * 
 * Turns on the external power supply (Vext) for the OLED display.
 * This is necessary for proper display operation on many LoRa boards
 * where the display power is controlled by a GPIO pin.
 */
void VextON(void)
{
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);    // LOW = power on for typical board configurations
}

/**
 * @brief Disable external power supply for OLED display
 * 
 * Turns off the external power supply (Vext) for the OLED display.
 * Used for power management when the display is not needed.
 * Default state is OFF for power conservation.
 */
void VextOFF(void) // Vext default OFF
{
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, HIGH);   // HIGH = power off for typical board configurations
}
