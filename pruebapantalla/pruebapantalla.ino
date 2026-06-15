#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Dirección más común: 0x27, si no funciona probá 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Hola emi!");
  lcd.setCursor(0, 1);
  lcd.print("Dispensador v1");
}

void loop() {}