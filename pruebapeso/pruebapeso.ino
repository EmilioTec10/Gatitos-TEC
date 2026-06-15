#include "HX711.h"

#define DOUT 4
#define CLK  5
#define UMBRAL 20.0

HX711 scale;
float pesoAnterior = 0;
bool hayPeso = false;

void setup() {
  Serial.begin(115200);
  scale.begin(DOUT, CLK);
  
  // Asegurate que NO haya nada en la celda al encender
  Serial.println("Tarando, quitá todo de la celda...");
  delay(3000);
  scale.tare();
  scale.set_scale(-450.0);
  Serial.println("Listo.");
}

void loop() {
  if (scale.is_ready()) {
    float peso = abs(scale.get_units(10));  // abs() ignora orientación
    
    if (peso > UMBRAL && !hayPeso) {
      hayPeso = true;
      Serial.print("Peso detectado: ");
      Serial.print(peso);
      Serial.println(" g");
    } else if (peso <= UMBRAL && hayPeso) {
      hayPeso = false;
      Serial.println("Sin peso.");
    }
  }
  delay(500);
}