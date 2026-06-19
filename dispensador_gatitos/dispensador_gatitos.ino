/*
 * ============================================================================
 *  Gatitos Tec - Dispensador inteligente de comida para gatos (CE5507)
 *  Firmware ESP32 (DOIT DevKit V1)
 * ----------------------------------------------------------------------------
 *  Maquina de estados de 3 estados observables: IDLE / DETECTING / DISPENSING
 *
 *  Flujo autonomo:
 *    IDLE       -> escanea el sensor IR continuamente.
 *    DETECTING  -> hay presencia: intenta leer un tag NFC (~5s de espera).
 *    DISPENSING -> el tag esta en la whitelist: dispensa, pesa el delta,
 *                  descuenta de la tolva y suena una melodia ascendente.
 *
 *  Tambien expone un servidor HTTP con CORS para que la app web consulte el
 *  estado, fuerce un dispensado manual y lea el historial.
 *
 *  Endpoints:
 *    GET  /status   -> { state, hopperRemaining, lastDispensed, plateWeight,
 *                        nfcCooldownRemaining, wifi }
 *    POST /dispense -> dispara un ciclo de dispensado manual (meta opcional ?grams=NN)
 *    GET  /log      -> [ { timestamp, grams, uid }, ... ]
 *    POST /tare     -> pone la balanza en cero (plato vacio)
 * ============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <HX711.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_PN532.h>

// ============================================================================
//  CONFIGURACION DE RED  -- COMPLETAR ANTES DE COMPILAR
// ============================================================================
#define WIFI_SSID "HEBONET 2.4G"
#define WIFI_PASSWORD "vrbnsl1470"   // <-- Cambia por la contrasena

// ============================================================================
//  PINES (todos cableados y probados individualmente)
// ============================================================================
#define IR_PIN       27   // Sensor IR E18-D80NK (LOW = objeto detectado)
#define BUZZER_PIN   25   // Buzzer pasivo (tone/noTone)
#define HX711_DT      4   // HX711 Data
#define HX711_SCK     5   // HX711 Clock
#define MOTOR_IN1    13   // L9110S IN1
#define MOTOR_IN2    14   // L9110S IN2
#define I2C_SDA      21   // Bus I2C compartido: LCD (0x27) + PN532 (0x24)
#define I2C_SCL      22

// ============================================================================
//  PARAMETROS AJUSTABLES
// ============================================================================
#define CALIBRATION_FACTOR -405.0   // Factor de calibracion de la celda HX711
#define HOPPER_CAPACITY     100     // Capacidad de la tolva en gramos (ajustar)
#define NFC_TIMEOUT_MS      5000    // Espera maxima para leer un tag en DETECTING
#define SETTLE_MS           300     // Espera para que la comida asiente antes de pesar
#define LOG_SIZE            20      // Cantidad de eventos guardados en el historial

// --- Control de dispensado por PESO OBJETIVO (lazo cerrado) ---
#define TOLERANCE_G         2       // Se detiene al llegar a (meta - TOLERANCE_G); bajo
                                    // para compensar la inercia de comida tras motorOff()
#define SAFETY_TIMEOUT_MS   10000   // Tiempo maximo de giro del motor (10s)
#define DISPENSE_TONE_HZ        800 // Tono pulsante mientras dispensa
#define DISPENSE_TONE_PULSE_MS  150 // Periodo de alternancia on/off del tono
#define MIN_TARGET_G        5       // Limite inferior valido para la meta
#define MAX_TARGET_G        200     // Limite superior valido para la meta

// --- Tamanos de porcion (gramos) para los 5 botones de la app ---
#define PORTION_1           15
#define PORTION_2           30
#define PORTION_3           45
#define PORTION_4           60
#define PORTION_5           75
#define DEFAULT_PORTION_G   PORTION_1   // Porcion por defecto (NFC y manual sin meta)

// ============================================================================
//  WHITELIST DE TAGS NFC AUTORIZADOS
//  Formato exacto: hex en mayusculas, dos digitos por byte, separados por espacio.
// ============================================================================
const char* WHITELIST[] = {
  "47 D6 0C 06",   // Llavero azul
  "02 8F 5C C5"    // Tag adhesivo
};
const int WHITELIST_LEN = sizeof(WHITELIST) / sizeof(WHITELIST[0]);

// ============================================================================
//  OBJETOS DE HARDWARE
// ============================================================================
HX711 scale;
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_PN532 nfc(I2C_SDA, I2C_SCL);   // Modo I2C (uso probado por el equipo)
WebServer server(80);

// ============================================================================
//  ESTADO GLOBAL
// ============================================================================
enum State { STATE_IDLE, STATE_DETECTING, STATE_DISPENSING };
State currentState = STATE_IDLE;
bool  stateJustChanged = true;          // Para refrescar la LCD solo al entrar a un estado

float hopperRemaining = HOPPER_CAPACITY; // Gramos restantes en la tolva
int   lastDispensed   = 0;               // Gramos del ultimo dispensado
float plateWeight     = 0;               // Peso actual sobre el plato (gramos)

// Contexto del dispensado no bloqueante (estado DISPENSING manejado en loop())
bool          dispenseActive   = false;  // hay un dispensado en curso
float         dispenseBaseline = 0;      // peso del plato al iniciar el ciclo
int           dispenseTarget   = 0;      // meta de gramos del ciclo actual
unsigned long dispenseStart    = 0;      // millis() de inicio del ciclo
String        dispenseUid      = "";     // UID (o "MANUAL") del ciclo actual
unsigned long idleCooldownUntil = 0;     // ignora el IR hasta este millis()
unsigned long dispenseToneLast = 0;      // millis() del ultimo cambio de pulso del tono
bool          dispenseToneOn   = false;  // estado actual del tono pulsante

// Cooldown SOLO para dispensados autonomos por NFC (los manuales no se limitan)
unsigned long lastNfcFeedMillis = 0;     // millis() del ultimo dispensado por NFC
#define MIN_NFC_INTERVAL_MS (15UL * 60UL * 1000UL)   // 15 minutos

// Buffer circular de eventos
struct LogEntry {
  unsigned long timestamp;  // millis() del evento
  int           grams;      // gramos dispensados
  String        uid;        // UID del tag o "MANUAL"
};
LogEntry logBuffer[LOG_SIZE];
int logCount = 0;   // cantidad de eventos guardados (hasta LOG_SIZE)
int logHead  = 0;   // indice donde se escribira el proximo evento

// ============================================================================
//  UTILIDADES
// ============================================================================

// Nombre legible del estado actual (para JSON y debug)
const char* stateName(State s) {
  switch (s) {
    case STATE_IDLE:       return "IDLE";
    case STATE_DETECTING:  return "DETECTING";
    case STATE_DISPENSING: return "DISPENSING";
  }
  return "UNKNOWN";
}

// Cambia de estado, marca la LCD como "sucia" e imprime la transicion por Serial
void setState(State newState) {
  if (newState != currentState) {
    Serial.printf("[ESTADO] %s -> %s\n", stateName(currentState), stateName(newState));
  }
  currentState = newState;
  stateJustChanged = true;
}

// Escribe dos lineas en la LCD.
// Re-inicializa el controlador en cada actualizacion: si el bus I2C se
// desincronizo por ruido (el buzzer pulsante comparte entorno con SDA/SCL) o
// por un glitch al arrancar, el LCD se veia con bloques negros / basura y NO
// se recuperaba porque solo se hacia clear(). Re-correr la secuencia de init
// del HD44780 lo deja limpio en cada mensaje (se llama solo en eventos
// discretos, no en bucles, asi que el costo ~50ms es irrelevante).
void lcdShow(const String& line1, const String& line2) {
  lcd.init();         // re-sincroniza el HD44780/PCF8574 (incluye clear)
  lcd.backlight();    // init puede apagar la retroiluminacion -> la reactivamos
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
}

// Convierte el UID leido a string "AA BB CC DD" (hex mayusculas)
String uidToString(uint8_t* uid, uint8_t len) {
  String result = "";
  for (uint8_t i = 0; i < len; i++) {
    if (uid[i] < 0x10) result += "0";
    result += String(uid[i], HEX);
    if (i < len - 1) result += " ";
  }
  result.toUpperCase();
  return result;
}

// Verifica si un UID esta en la whitelist
bool isWhitelisted(const String& uid) {
  for (int i = 0; i < WHITELIST_LEN; i++) {
    if (uid.equals(WHITELIST[i])) return true;
  }
  return false;
}

// Guarda un evento en el buffer circular
void addLogEntry(int grams, const String& uid) {
  logBuffer[logHead].timestamp = millis();
  logBuffer[logHead].grams     = grams;
  logBuffer[logHead].uid       = uid;
  logHead = (logHead + 1) % LOG_SIZE;
  if (logCount < LOG_SIZE) logCount++;
}

// ============================================================================
//  ACTUADORES: MOTOR Y BUZZER
// ============================================================================
void motorOn()  { digitalWrite(MOTOR_IN1, HIGH); digitalWrite(MOTOR_IN2, LOW); }
void motorOff() { digitalWrite(MOTOR_IN1, LOW);  digitalWrite(MOTOR_IN2, LOW); }

// Melodia ascendente de 4 notas (dispensado exitoso): C5 - E5 - G5 - C6
void successChime() {
  const int notes[] = {523, 659, 784, 1047};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, notes[i], 150);
    delay(180);
  }
  noTone(BUZZER_PIN);
}

// Tono grave de 2 notas (tag no reconocido)
void denialTone() {
  tone(BUZZER_PIN, 220, 250);
  delay(300);
  tone(BUZZER_PIN, 165, 400);
  delay(450);
  noTone(BUZZER_PIN);
}

// WiFi conectado: chime ascendente corto C5-E5-G5 (~0.5s). Solo al boot.
void wifiOkChime() {
  const int notes[] = {523, 659, 784};
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, notes[i], 150);
    delay(180);
  }
  noTone(BUZZER_PIN);
}

// WiFi fallido: 3 pulsos cortos al mismo tono grave (G3 = 196Hz). El ritmo
// staccato lo hace claramente distinto del denialTone NFC (2 notas descendentes
// sostenidas 220->165). Solo al boot.
void wifiFailTone() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 196, 150);
    delay(230);
  }
  noTone(BUZZER_PIN);
}

// ============================================================================
//  DISPENSADO POR PESO OBJETIVO - NO BLOQUEANTE
//  Compartido por el NFC autonomo y por /dispense manual.
//  startDispense() arranca el ciclo y regresa de inmediato; handleDispensing()
//  corre dentro de loop() para que el servidor web siga atendiendo peticiones.
// ============================================================================

// Arranca un ciclo de dispensado. NO espera; regresa enseguida.
void startDispense(const String& uid, int targetGrams) {
  setState(STATE_DISPENSING);
  dispenseActive   = true;
  dispenseUid      = uid;
  dispenseTarget   = targetGrams;
  dispenseStart    = millis();
  dispenseBaseline = scale.get_units(10);   // peso de referencia del plato

  // ---- DEBUG TEMPORAL: linea base ----
  Serial.printf("[DBG] === Inicio dispensado === baseline(10)=%.2f g | meta=%d g | corte=%d g\n",
                dispenseBaseline, targetGrams, targetGrams - TOLERANCE_G);
  // ------------------------------------

  Serial.printf("[DISPENSANDO] Iniciado por UID: %s | meta: %d g\n", uid.c_str(), targetGrams);
  lcdShow("Dispensando...", "Meta: " + String(targetGrams) + "g");
  motorOn();

  // Arranca el tono pulsante (continuo, sin duracion, hasta noTone())
  dispenseToneLast = millis();
  dispenseToneOn   = true;
  tone(BUZZER_PIN, DISPENSE_TONE_HZ);
}

// Termina el ciclo: para el motor, mide los gramos reales y registra el evento.
void finishDispense(bool timedOut) {
  // Corta el tono pulsante de inmediato (antes del chime final)
  noTone(BUZZER_PIN);
  dispenseToneOn = false;

  motorOff();

  // ---- DEBUG TEMPORAL: lectura justo al apagar el motor (antes de asentar) ----
  float preSettle = scale.get_units(2);
  Serial.printf("[DBG] motorOff. preSettle raw(2)=%.2fg | aplicando settle=%dms\n",
                preSettle, SETTLE_MS);
  // ----------------------------------------------------------------------------

  delay(SETTLE_MS);   // deja que la comida asiente antes de la medicion final

  // Medicion final estable -> gramos realmente dispensados.
  // Valor absoluto: igual que en el lazo, soporta la celda en cualquier orientacion.
  float after = scale.get_units(10);
  float grams = fabs(after - dispenseBaseline);

  // ---- DEBUG TEMPORAL: medicion final detallada (sub-gramo) ----
  Serial.printf("[DBG] postSettle after(10)=%.2fg baseline=%.2fg grams=%.2fg -> round=%d\n",
                after, dispenseBaseline, grams, (int)round(grams));
  // --------------------------------------------------------------

  // Actualiza estado global con los gramos REALES (no la meta)
  lastDispensed   = (int)round(grams);
  plateWeight     = after;
  hopperRemaining -= grams;
  if (hopperRemaining < 0) hopperRemaining = 0;

  if (timedOut) {
    Serial.println("[DISPENSE] Timeout de seguridad alcanzado");
  }

  lcdShow("Dispensando...", String(lastDispensed) + "g");
  successChime();
  addLogEntry(lastDispensed, dispenseUid);

  Serial.printf("[DISPENSANDO] Meta %d g -> %d g reales. Tolva restante: %d g\n",
                dispenseTarget, lastDispensed, (int)hopperRemaining);

  dispenseActive   = false;
  idleCooldownUntil = millis() + 1000;   // evita re-disparo inmediato del IR
  setState(STATE_IDLE);
}

// Estado DISPENSING: una lectura rapida por iteracion (el motor sigue girando
// entre lecturas) para no bloquear el loop ni el servidor web.
void handleDispensing() {
  // Tono pulsante: alterna on/off por tiempo (millis), sin delay ni bloqueo
  if (millis() - dispenseToneLast >= DISPENSE_TONE_PULSE_MS) {
    dispenseToneLast = millis();
    dispenseToneOn = !dispenseToneOn;
    if (dispenseToneOn) tone(BUZZER_PIN, DISPENSE_TONE_HZ);
    else                noTone(BUZZER_PIN);
  }

  float current = scale.get_units(2);   // ~200ms; loop() vuelve a atender al server
  float rawDelta = current - dispenseBaseline;   // delta con signo (puede ser negativo)

  // Magnitud del delta: la celda puede leer en negativo segun su orientacion,
  // asi que comparamos por valor absoluto y el sistema funciona en cualquier sentido.
  float dispensed = fabs(rawDelta);

  // ---- DEBUG TEMPORAL: curva de peso en vivo (cada ~200ms) ----
  Serial.printf("[DBG] t=%lums raw=%.2fg delta=%.2fg |delta|=%.2fg\n",
                millis() - dispenseStart, current, rawDelta, dispensed);
  // -------------------------------------------------------------

  if (dispensed >= dispenseTarget - TOLERANCE_G) {
    // ---- DEBUG TEMPORAL ----
    Serial.printf("[DBG] CORTE por META a t=%lums (delta=%.2f >= %d)\n",
                  millis() - dispenseStart, dispensed, dispenseTarget - TOLERANCE_G);
    // ------------------------
    finishDispense(false);              // meta alcanzada
  } else if (millis() - dispenseStart >= SAFETY_TIMEOUT_MS) {
    // ---- DEBUG TEMPORAL ----
    Serial.printf("[DBG] CORTE por TIMEOUT a t=%lums\n", millis() - dispenseStart);
    // ------------------------
    finishDispense(true);               // timeout de seguridad
  }
}

// ============================================================================
//  ESTADOS DE LA MAQUINA
// ============================================================================

// IDLE: escanea el sensor IR. Refresca la LCD solo al entrar.
void handleIdle() {
  if (stateJustChanged) {
    lcdShow("Tolva: " + String((int)hopperRemaining) + "g", "Listo");
    stateJustChanged = false;
  }

  // Ignora el IR durante el periodo de enfriamiento tras un dispensado
  if (millis() < idleCooldownUntil) return;

  // El E18-D80NK entrega LOW cuando detecta un objeto
  if (digitalRead(IR_PIN) == LOW) {
    Serial.println("[IR] Presencia detectada");
    setState(STATE_DETECTING);
  }
}

// DETECTING: hay presencia -> intenta leer un tag NFC durante NFC_TIMEOUT_MS.
void handleDetecting() {
  if (stateJustChanged) {
    lcdShow("Detectado", "Esperando...");
    stateJustChanged = false;
  }

  uint8_t uid[7] = {0};
  uint8_t uidLen = 0;
  unsigned long start = millis();
  bool found = false;

  // Sondea el lector sin dejar de atender al servidor web
  while (millis() - start < NFC_TIMEOUT_MS) {
    server.handleClient();
    // Si llega un /dispense manual mientras detectamos, ese ciclo toma el
    // control: salimos y dejamos que el estado DISPENSING lo maneje.
    if (dispenseActive) return;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 500)) {
      found = true;
      break;
    }
  }

  if (!found) {
    Serial.println("[NFC] Sin tag dentro del tiempo de espera");
    idleCooldownUntil = millis() + 500;   // pausa breve anti re-disparo
    setState(STATE_IDLE);
    return;
  }

  String uidStr = uidToString(uid, uidLen);
  Serial.printf("[NFC] Tag leido: %s\n", uidStr.c_str());

  if (isWhitelisted(uidStr)) {
    // Cooldown SOLO para NFC: si aun no paso el intervalo minimo, no dispensa.
    // (lastNfcFeedMillis != 0 -> el primer dispensado tras encender no se bloquea)
    if (lastNfcFeedMillis != 0 && millis() - lastNfcFeedMillis < MIN_NFC_INTERVAL_MS) {
      unsigned long restanteMs = MIN_NFC_INTERVAL_MS - (millis() - lastNfcFeedMillis);
      int restanteMin = (restanteMs + 59999UL) / 60000UL;   // redondeo hacia arriba
      Serial.printf("[NFC] En cooldown: faltan %d min\n", restanteMin);
      lcdShow("Espera", String(restanteMin) + " min");
      denialTone();                          // reutiliza el tono de denegacion
      delay(800);
      idleCooldownUntil = millis() + 500;    // evita re-disparo si el gato sigue presente
      setState(STATE_IDLE);
      return;
    }

    // Cooldown superado: marca el momento y arranca el dispensado no bloqueante.
    lastNfcFeedMillis = millis();
    startDispense(uidStr, DEFAULT_PORTION_G);   // NFC usa la porcion por defecto
  } else {
    // Tag no autorizado: aviso y regreso a IDLE
    Serial.println("[NFC] Tag NO autorizado");
    lcdShow("Gato no", "reconocido");
    denialTone();
    delay(800);
    idleCooldownUntil = millis() + 500;   // evita re-disparo si el gato sigue presente
    setState(STATE_IDLE);
  }
}

// ============================================================================
//  SERVIDOR HTTP + CORS
// ============================================================================

// Agrega los encabezados CORS a la respuesta en curso
void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// Responde a las peticiones OPTIONS (preflight CORS)
void handleOptions() {
  sendCorsHeaders();
  server.send(204);
}

// GET /status
void handleStatus() {
  // Solo lee la celda si NO se esta dispensando: durante el ciclo la celda la
  // usa handleDispensing(), asi que devolvemos el ultimo plateWeight conocido.
  if (!dispenseActive) {
    plateWeight = scale.get_units(5);
  }

  // Segundos restantes de cooldown NFC (0 si no hay o ya paso)
  int nfcCooldownRemaining = 0;
  if (lastNfcFeedMillis != 0) {
    unsigned long sinceNfc = millis() - lastNfcFeedMillis;
    if (sinceNfc < MIN_NFC_INTERVAL_MS) {
      nfcCooldownRemaining = (MIN_NFC_INTERVAL_MS - sinceNfc) / 1000UL;
    }
  }

  String json = "{";
  json += "\"state\":\"" + String(stateName(currentState)) + "\",";
  json += "\"hopperRemaining\":" + String((int)hopperRemaining) + ",";
  json += "\"lastDispensed\":" + String(lastDispensed) + ",";
  json += "\"plateWeight\":" + String((int)round(plateWeight)) + ",";
  json += "\"nfcCooldownRemaining\":" + String(nfcCooldownRemaining) + ",";
  json += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += "}";

  sendCorsHeaders();
  server.send(200, "application/json", json);
}

// POST /dispense -> dispensado manual desde la app.
// Acepta una meta OPCIONAL "grams" como query param (?grams=NN) o en el
// cuerpo JSON {"grams":NN}. Si no se envia, usa DEFAULT_PORTION_G (15g).
// NO bloquea: arranca el ciclo y responde de inmediato (la app sondea /status).
void handleDispense() {
  // Si ya hay un dispensado en curso, no arranca otro
  if (dispenseActive) {
    sendCorsHeaders();
    server.send(409, "application/json", "{\"ok\":false,\"busy\":true}");
    return;
  }

  int target = DEFAULT_PORTION_G;

  if (server.hasArg("grams")) {
    // Forma preferida por la app: query param (no dispara preflight CORS)
    target = server.arg("grams").toInt();
  } else if (server.hasArg("plain")) {
    // Alternativa: cuerpo JSON {"grams":NN}
    String body = server.arg("plain");
    int idx = body.indexOf("\"grams\"");
    if (idx >= 0) {
      int colon = body.indexOf(':', idx);
      if (colon >= 0) target = body.substring(colon + 1).toInt();
    }
  }

  // Valida/acota la meta a un rango sano
  if (target < MIN_TARGET_G) target = MIN_TARGET_G;
  if (target > MAX_TARGET_G) target = MAX_TARGET_G;

  Serial.printf("[HTTP] /dispense solicitado (meta %d g)\n", target);
  startDispense("MANUAL", target);   // arranca el ciclo; no espera a que termine

  String json = "{";
  json += "\"ok\":true,";
  json += "\"accepted\":true,";
  json += "\"target\":" + String(target);
  json += "}";

  sendCorsHeaders();
  server.send(202, "application/json", json);
}

// GET /log -> arreglo de eventos, del mas reciente al mas antiguo
void handleLog() {
  String json = "[";
  for (int i = 0; i < logCount; i++) {
    // recorre el buffer circular hacia atras desde el ultimo escrito
    int idx = (logHead - 1 - i + LOG_SIZE) % LOG_SIZE;
    if (i > 0) json += ",";
    json += "{";
    json += "\"timestamp\":" + String(logBuffer[idx].timestamp) + ",";
    json += "\"grams\":" + String(logBuffer[idx].grams) + ",";
    json += "\"uid\":\"" + logBuffer[idx].uid + "\"";
    json += "}";
  }
  json += "]";

  sendCorsHeaders();
  server.send(200, "application/json", json);
}

// POST /tare -> pone el plato vacio en cero (reinicia la referencia de la celda)
void handleTare() {
  // No tarar con el motor girando / dispensado en curso
  if (dispenseActive) {
    sendCorsHeaders();
    server.send(409, "application/json", "{\"ok\":false,\"busy\":true}");
    return;
  }

  Serial.println("[HTTP] /tare solicitado");
  scale.tare();
  plateWeight = 0;

  sendCorsHeaders();
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"tare\"}");
}

void setupServer() {
  server.on("/status",   HTTP_GET,     handleStatus);
  server.on("/dispense", HTTP_POST,    handleDispense);
  server.on("/dispense", HTTP_OPTIONS, handleOptions);
  server.on("/tare",     HTTP_POST,    handleTare);
  server.on("/tare",     HTTP_OPTIONS, handleOptions);
  server.on("/log",      HTTP_GET,     handleLog);

  // Cualquier otra ruta: responde OPTIONS para CORS o 404 con CORS
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
      sendCorsHeaders();
      server.send(404, "application/json", "{\"error\":\"not found\"}");
    }
  });

  server.begin();
  Serial.println("[HTTP] Servidor iniciado en el puerto 80");
}

// ============================================================================
//  CONEXION WIFI
// ============================================================================
void connectWiFi() {
  Serial.printf("[WiFi] Conectando a %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lcdShow("Conectando WiFi", "...");

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiOkChime();                       // chime de exito antes del mensaje en LCD
    Serial.println();
    Serial.print("[WiFi] Conectado. IP: ");
    Serial.println(WiFi.localIP());
    lcdShow("WiFi OK", WiFi.localIP().toString());
    delay(2000);
  } else {
    Serial.println("\n[WiFi] FALLO la conexion (revisa SSID/PASSWORD)");
    lcdShow("Sin WiFi", "Error conexion");
    wifiFailTone();                      // tono de fallo (distinto del NFC denial)
    delay(2000);
  }
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Gatitos Tec - Dispensador inteligente ===");

  // Sensor IR y motor
  pinMode(IR_PIN, INPUT_PULLUP);
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  motorOff();

  // Bus I2C compartido (LCD 0x27 + PN532 0x24)
  Wire.begin(I2C_SDA, I2C_SCL);

  // LCD
  lcd.init();
  lcd.backlight();
  lcdShow("Gatitos Tec", "Iniciando...");

  // Celda de carga HX711
  scale.begin(HX711_DT, HX711_SCK);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare();   // pone el plato vacio en cero
  Serial.println("[HX711] Calibrado y tarado");

  // Lector NFC PN532
  nfc.begin();
  uint32_t version = nfc.getFirmwareVersion();
  if (!version) {
    Serial.println("[NFC] PN532 NO encontrado. Revisa el bus I2C.");
    lcdShow("Error NFC", "Revisar I2C");
    delay(2000);
  } else {
    Serial.printf("[NFC] PN532 OK (firmware 0x%08X)\n", version);
    nfc.SAMConfig();
  }

  // WiFi + servidor HTTP
  connectWiFi();
  setupServer();

  // Arranca en IDLE
  setState(STATE_IDLE);
  Serial.println("=== Sistema listo ===");
}

// ============================================================================
//  LOOP: atiende el servidor y corre la maquina de estados
// ============================================================================
void loop() {
  server.handleClient();

  switch (currentState) {
    case STATE_IDLE:       handleIdle();       break;
    case STATE_DETECTING:  handleDetecting();  break;
    case STATE_DISPENSING: handleDispensing(); break;
  }
}
