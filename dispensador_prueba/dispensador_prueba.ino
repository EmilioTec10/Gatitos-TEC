
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"

// ── Pines ──────────────────────────────────────────
#define IR_PIN     27
#define BUZZER_PIN 25
#define HX711_DT   4
#define HX711_CLK  5
#define SDA_PIN    21
#define SCL_PIN    22

// ── Whitelist UIDs ─────────────────────────────────
// Agregá más UIDs según necesiten
uint8_t whitelist[][7] = {
  {0x47, 0xD6, 0x0C, 0x06},  // Tag azul keyfob
  {0x02, 0x8F, 0x5C, 0xC5},  // Tag sticker
};
uint8_t whitelistSizes[] = {4, 4};
const int totalTags = 2;

// ── Objetos ────────────────────────────────────────
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 scale;

// ── Helpers ────────────────────────────────────────
bool uidEnWhitelist(uint8_t *uid, uint8_t uidLen) {
  for (int i = 0; i < totalTags; i++) {
    if (uidLen == whitelistSizes[i]) {
      bool match = true;
      for (int j = 0; j < uidLen; j++) {
        if (uid[j] != whitelist[i][j]) { match = false; break; }
      }
      if (match) return true;
    }
  }
  return false;
}

void melodiaDispensa() {
  // Melodía corta y amigable (4 notas)
  tone(BUZZER_PIN, 523, 150); delay(180); // C5
  tone(BUZZER_PIN, 659, 150); delay(180); // E5
  tone(BUZZER_PIN, 784, 150); delay(180); // G5
  tone(BUZZER_PIN, 1047, 300); delay(350); // C6
  noTone(BUZZER_PIN);
}

void melodiaDenegado() {
  tone(BUZZER_PIN, 300, 400); delay(450);
  tone(BUZZER_PIN, 200, 600); delay(650);
  noTone(BUZZER_PIN);
}

void lcdMensaje(String linea1, String linea2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(linea1);
  if (linea2 != "") {
    lcd.setCursor(0, 1);
    lcd.print(linea2);
  }
}

// ── Setup ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(IR_PIN, INPUT);

  // LCD
  lcd.init();
  lcd.backlight();
  lcdMensaje("Iniciando...");

  // NFC
  nfc.begin();
  if (!nfc.getFirmwareVersion()) {
    Serial.println("PN532 no encontrado");
    lcdMensaje("Error:", "NFC no encontrado");
    while (1);
  }
  nfc.SAMConfig();

  // HX711
  scale.begin(HX711_DT, HX711_CLK);
  delay(2000);
  scale.tare();
  scale.set_scale(-450.0);

  Serial.println("Sistema listo.");
  lcdMensaje("Sistema listo", "Esperando gato...");
}

// ── Loop ───────────────────────────────────────────
void loop() {
  // 1. Esperar presencia por IR
  if (digitalRead(IR_PIN) == LOW) {  // LOW = objeto detectado en E18-D80NK
    Serial.println("Presencia detectada");
    lcdMensaje("Acerca tu tag", "NFC...");

    // 2. Esperar tag NFC (timeout 5 segundos)
    uint8_t uid[7];
    uint8_t uidLen;
    unsigned long inicio = millis();
    bool tagLeido = false;

    while (millis() - inicio < 5000) {
      if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 500)) {
        tagLeido = true;
        break;
      }
    }

    if (!tagLeido) {
      Serial.println("Timeout NFC");
      lcdMensaje("Sin tag", "detectado");
      delay(2000);
      lcdMensaje("Esperando gato...");
      return;
    }

    // 3. Verificar whitelist
    if (uidEnWhitelist(uid, uidLen)) {
      Serial.println("Tag autorizado - dispensando");
      lcdMensaje("Autorizado!", "Dispensando...");
      melodiaDispensa();

      // 4. Medir peso (simulado por ahora sin servo)
      delay(1000);
      float peso = abs(scale.get_units(10));
      Serial.print("Peso dispensado: ");
      Serial.print(peso);
      Serial.println(" g");

      lcdMensaje("Dispensado:", String(peso, 1) + " g");
      delay(3000);

    } else {
      Serial.println("Tag NO autorizado");
      lcdMensaje("Acceso", "denegado");
      melodiaDenegado();
      delay(2000);
    }

    lcdMensaje("Esperando gato...");
  }

  delay(200);
}
