/*
 * ============================================================================
 *  Sensor E18-D80NK + Buzzer pasivo -> Chime "comida lista"
 *  ESP32 DevKit (WROOM)
 * ============================================================================
 *
 *  Conexion:
 *    E18-D80NK:
 *      Cafe  (VCC) -> 5V (VIN)
 *      Azul  (GND) -> GND
 *      Negro (OUT) -> S del GPIO 27, con pull-up 10K a 3.3V
 *
 *    Buzzer pasivo:
 *      Pin + -> S del GPIO 25
 *      Pin - -> G del GPIO 25
 * ============================================================================
 */

#define IR_PIN     27
#define BUZZER_PIN 25

// Chime: 4 notas ascendentes suaves, amigable para gatos
// C5 -> E5 -> G5 -> C6  (acorde de Do mayor ascendente)
void tocarChime() {
  tone(BUZZER_PIN, 523); delay(180); noTone(BUZZER_PIN); delay(60);  // C5
  tone(BUZZER_PIN, 659); delay(180); noTone(BUZZER_PIN); delay(60);  // E5
  tone(BUZZER_PIN, 784); delay(180); noTone(BUZZER_PIN); delay(60);  // G5
  tone(BUZZER_PIN, 1047); delay(350); noTone(BUZZER_PIN);            // C6 (nota larga de cierre)
}

void setup() {
  Serial.begin(115200);
  pinMode(IR_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  Serial.println("Listo. Esperando deteccion...");
}

void loop() {
  bool detectado = (digitalRead(IR_PIN) == LOW);

  if (detectado) {
    Serial.println("Gato detectado -> chime");
    tocarChime();
    // Espera a que el objeto se retire antes de volver a disparar
    while (digitalRead(IR_PIN) == LOW) {
      delay(100);
    }
    delay(500);
    Serial.println("Listo para el siguiente.");
  }

  delay(100);
}
