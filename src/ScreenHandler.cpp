#include <ScreenHandler.h>

void VextON(void);
void VextOFF(void);

static SSD1306Wire factory_display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_64_32, RST_OLED);
ScreenHandler display_screen = ScreenHandler();

ScreenHandler::ScreenHandler()
{
    status_line = "Boot";
    line1 = "";
    line2 = "";
}

void ScreenHandler::draw_screen()
{
    factory_display.drawString(0, 0, status_line);
    factory_display.drawString(0, 10, line1);
    factory_display.drawString(0, 20, line2);
    factory_display.display();
    factory_display.clear();
}

void ScreenHandler::update_status(String str)
{
    status_line = str;
    draw_screen();

}

void ScreenHandler::add_line(String str)
{
    line1 = line2;
    line2 = str;
    draw_screen();
}

void ScreenHandler::setup()
{
	// Turn LED Off
	pinMode(LED ,OUTPUT);
	digitalWrite(LED, LOW);  

	// Init the display
	VextON();
	factory_display.init();
	factory_display.clear();
    draw_screen();
    delay(500);
}

void VextON(void)
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, LOW);
  
}

void VextOFF(void) //Vext default OFF
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, HIGH);
}
