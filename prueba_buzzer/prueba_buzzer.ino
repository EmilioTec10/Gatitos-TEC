/*
 * ============================================================================
 *  PRUEBA AISLADA - Buzzer pasivo
 *  ESP32 DevKit (WROOM)
 * ============================================================================
 *
 *  Conexion:
 *    Pin + (señal) -> S del GPIO 25
 *    Pin -        -> G del GPIO 25
 *
 *  El buzzer pasivo necesita una señal PWM para sonar.
 *  tone() genera esa señal — por eso cambia de tonalidad con cada frecuencia.
 *
 *  NOTA: Requiere ESP32 Arduino core 3.x para que tone() funcione.
 * ============================================================================
 */

#define BUZZER_PIN 25

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  Serial.println("== Prueba buzzer pasivo ==");
}

void loop() {
  Serial.println("DO  - 523 Hz");
  tone(BUZZER_PIN, 523); delay(400); noTone(BUZZER_PIN); delay(100);

  Serial.println("RE  - 587 Hz");
  tone(BUZZER_PIN, 587); delay(400); noTone(BUZZER_PIN); delay(100);

  Serial.println("MI  - 659 Hz");
  tone(BUZZER_PIN, 659); delay(400); noTone(BUZZER_PIN); delay(100);

  Serial.println("FA  - 698 Hz");
  tone(BUZZER_PIN, 698); delay(400); noTone(BUZZER_PIN); delay(100);

  Serial.println("SOL - 784 Hz");
  tone(BUZZER_PIN, 784); delay(400); noTone(BUZZER_PIN); delay(100);

  Serial.println("LA  - 880 Hz");
  tone(BUZZER_PIN, 880); delay(400); noTone(BUZZER_PIN); delay(100);

  Serial.println("SI  - 988 Hz");
  tone(BUZZER_PIN, 988); delay(400); noTone(BUZZER_PIN); delay(100);

  Serial.println("---");
  delay(800);
}
