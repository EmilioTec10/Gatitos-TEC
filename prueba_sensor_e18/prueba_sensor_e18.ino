/*
 * ============================================================================
 *  PRUEBA AISLADA - Sensor IR E18-D80NK
 *  ESP32 DevKit (WROOM)
 * ============================================================================
 *
 *  Conexion:
 *    Cable cafe/marron (VCC)  -> 5V (pin VIN del ESP32)
 *    Cable azul        (GND)  -> GND
 *    Cable negro       (OUT)  -> GPIO 27, con pull-up de 10K a 3.3V
 *
 *  Logica del sensor (segun el manual ETT):
 *    objeto cerca  (<= distancia ajustada) -> OUTPUT = 0 (LOW),  LED ENCENDIDO
 *    sin objeto    (>= distancia ajustada) -> OUTPUT = 1 (HIGH), LED APAGADO
 * ============================================================================
 */

#define IR_PIN 27

void setup() {
  Serial.begin(115200);
  delay(300);

  // Pull-up interno como respaldo; el pull-up externo de 10K a 3.3V manda.
  pinMode(IR_PIN, INPUT_PULLUP);

  Serial.println();
  Serial.println("== Prueba sensor E18-D80NK ==");
  Serial.println("Acerca y aleja un objeto frente al sensor...");
}

void loop() {
  int valor = digitalRead(IR_PIN);     // 0 = detecta, 1 = sin objeto
  bool detectado = (valor == LOW);

  Serial.print("Lectura cruda: ");
  Serial.print(valor);
  Serial.print("   ->   ");
  Serial.println(detectado ? "OBJETO DETECTADO" : "sin objeto");

  delay(300);
}
