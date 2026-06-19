# Gatitos Tec — Dispensador Inteligente de Comida para Gatos

Proyecto del curso **CE5507 - Modelación Hardware Software con Orientación a Objetos** (TEC).

## Descripción

**Gatitos Tec** es un dispensador inteligente de comida para gatos basado en un
**ESP32**. El sistema alimenta de forma **autónoma**: detecta la presencia de un gato
con un sensor infrarrojo, lo identifica con un tag **NFC** y, si está autorizado,
dispensa una porción controlada **por peso** (no por tiempo) usando una celda de carga.

Resuelve el problema de la alimentación descontrolada: solo alimenta a gatos
registrados, sirve una cantidad medida en gramos y evita el sobrealimentado aplicando
un **intervalo mínimo de 15 minutos** entre alimentaciones automáticas. Además expone
un **servidor web** para monitorear el estado en tiempo real (nivel de tolva, peso en
plato, historial) y forzar una alimentación manual desde una app web.

## Integrantes

> _Completar con los nombres del equipo:_

- Nombre 1 — carné
- Nombre 2 — carné
- Nombre 3 — carné

## Componentes de hardware y mapeo de pines

Microcontrolador: **ESP32 DOIT DevKit V1**.

| Componente | Modelo | Conexión (GPIO) | Notas |
|---|---|---|---|
| Sensor de proximidad IR | E18-D80NK | `IR_PIN = 27` | `LOW` = objeto detectado, `INPUT_PULLUP` |
| Buzzer pasivo | — | `BUZZER_PIN = 25` | `tone()` / `noTone()` |
| Amplificador de celda de carga | HX711 (celda YZC-131A 5 kg) | `DT = 4`, `SCK = 5` | Factor de calibración `-405.0` |
| Pantalla LCD | LCD 16x2 I2C | `SDA = 21`, `SCL = 22` | Dirección I2C `0x27` |
| Lector NFC | PN532 (modo I2C) | `SDA = 21`, `SCL = 22` | Dirección I2C `0x24` (bus I2C compartido con LCD) |
| Driver de motor | L9110S (motorreductor 175 RPM) | `IN1 = 13`, `IN2 = 14` | `IN1=HIGH, IN2=LOW` gira; ambos `LOW` detiene |

> El LCD (`0x27`) y el PN532 (`0x24`) **comparten el bus I2C** (SDA=21, SCL=22) con
> direcciones distintas, por lo que conviven sin conflicto.

## Stack de software

- **Firmware:** Arduino para ESP32 (`dispensador_gatitos/dispensador_gatitos.ino`).
  Servidor HTTP con `<WebServer.h>`, máquina de estados y control de dispensado por
  peso objetivo (lazo cerrado).
- **Frontend:** Aplicación web **sin dependencias** — HTML + CSS + JavaScript vanilla
  con estructura **orientada a objetos** (`script.js`: clases `ESP32Client` y
  `DispenserApp`).
- **Sin herramientas de build:** no requiere Node, npm ni bundlers. El frontend se abre
  directo en el navegador.

## Instalación y configuración

### 1. Librerías de Arduino requeridas

Instálalas desde el **Library Manager** del Arduino IDE (usar exactamente estas):

| Librería | Autor |
|---|---|
| **HX711 Arduino Library** | Bogdan Necula (bogde) |
| **LiquidCrystal I2C** | Frank de Brabander |
| **Adafruit PN532** | Adafruit |

`WiFi.h`, `WebServer.h` y `Wire.h` vienen incluidas en el **core de ESP32** para Arduino.

### 2. Configurar el firmware antes de flashear

En `dispensador_gatitos/dispensador_gatitos.ino`, edita estas constantes en la parte
superior del archivo:

```cpp
// Credenciales de tu red WiFi
#define WIFI_SSID     "TU_SSID_AQUI"
#define WIFI_PASSWORD "TU_PASSWORD_AQUI"

// Capacidad de la tolva en gramos (ajusta a tu hardware real)
#define HOPPER_CAPACITY 100
```

### 3. Flashear el ESP32

1. En el Arduino IDE selecciona la placa: **DOIT ESP32 DEVKIT V1**.
2. Selecciona el puerto serie correcto.
3. **Verify** y luego **Upload**.

### 4. Encontrar la IP del ESP32 y configurarla en el frontend

1. Abre el **Monitor Serie a 115200 baudios**.
2. Al conectarse al WiFi, el ESP32 imprime su IP (y la muestra en el LCD):
   ```
   [WiFi] Conectado. IP: 192.168.x.y
   ```
3. Pon esa IP en `script.js`:
   ```js
   const ESP32_URL = "http://192.168.x.y";
   ```
4. Asegúrate de que `HOPPER_CAPACITY_G` en `script.js` **coincida** con
   `HOPPER_CAPACITY` del firmware:
   ```js
   const HOPPER_CAPACITY_G = 100;
   ```

> Verificación rápida: abre `http://192.168.x.y/status` en el navegador; debe
> devolver un JSON.

### 5. Ejecutar la app web

Abre `index.html` directamente en el navegador (doble clic o arrastrarlo a una
pestaña). **No requiere servidor ni build.** El dispositivo y la computadora/teléfono
deben estar en la **misma red WiFi** que el ESP32.

## Uso / funcionamiento

### Máquina de estados (3 estados)

| Estado | Qué hace | Transición |
|---|---|---|
| **IDLE** | Escanea el sensor IR continuamente. LCD: `Tolva: {g} / Listo`. | Si el IR detecta presencia (`LOW`) → **DETECTING** |
| **DETECTING** | Intenta leer un tag NFC (~5 s). LCD: `Detectado / Esperando...`. | Tag válido → **DISPENSING**; sin tag o no autorizado → **IDLE** |
| **DISPENSING** | Gira el motor hasta alcanzar el peso objetivo o el timeout de seguridad, mide el delta real, descuenta de la tolva y suena la melodía. | Al terminar → **IDLE** |

El dispensado es **no bloqueante**: el estado `DISPENSING` se procesa por partes en
`loop()` para que el servidor web siga respondiendo durante el ciclo.

### Alimentación manual vs. autónoma (NFC)

- **Manual (app web):** el botón **Alimentar Manual** envía `POST /dispense` con la
  porción seleccionada en los botones (15/30/45/60/75 g). **No tiene cooldown** —
  funciona siempre que el ESP32 esté conectado.
- **Autónoma (NFC):** al acercarse un gato y escanear un tag **autorizado**, el sistema
  dispensa la porción por defecto (**15 g**). Aplica un **intervalo mínimo de 15
  minutos** entre alimentaciones automáticas: si un gato vuelve antes de tiempo, el
  LCD muestra `Espera / {min} min`, suena el tono de denegación y **no** dispensa.
  Este cooldown **solo afecta al NFC**, nunca al manual. La app muestra un aviso sutil
  ("NFC en espera: X min restantes") leyendo `nfcCooldownRemaining` de `/status`.

### Registrar / autorizar un nuevo tag NFC

Los tags autorizados están en la **whitelist** del firmware. Edita el arreglo
`WHITELIST` en `dispensador_gatitos.ino` y vuelve a flashear:

```cpp
const char* WHITELIST[] = {
  "47 D6 0C 06",   // Llavero azul
  "02 8F 5C C5"    // Tag adhesivo
};
```

> El formato del UID es **hex en mayúsculas, dos dígitos por byte, separados por
> espacio**. Para conocer el UID de un tag nuevo, acércalo al lector y revisa el
> Monitor Serie: imprime `[NFC] Tag leido: XX XX XX XX`. Copia ese valor a la lista.
> Para asociar el UID a un nombre de gato en la app, edita `UID_TO_CAT` en `script.js`.

## Referencia de la API

Todos los endpoints incluyen cabeceras **CORS** (`Access-Control-Allow-Origin: *`).
Base: `http://<IP_DEL_ESP32>`.

### `GET /status`

Estado actual del dispositivo.

```json
{
  "state": "IDLE",
  "hopperRemaining": 85,
  "lastDispensed": 13,
  "plateWeight": 13,
  "nfcCooldownRemaining": 742,
  "wifi": true
}
```

- `state`: `IDLE` | `DETECTING` | `DISPENSING`
- `hopperRemaining`, `lastDispensed`, `plateWeight`: gramos
- `nfcCooldownRemaining`: segundos restantes del cooldown NFC (`0` si no hay)

### `POST /dispense`

Inicia un dispensado manual. Meta opcional en gramos como query param `?grams=NN`
(si se omite, usa 15 g). Responde de inmediato (`202`) sin esperar a que termine el
ciclo; la app sondea `/status` para ver el resultado.

```
POST /dispense?grams=45
```
```json
{ "ok": true, "accepted": true, "target": 45 }
```

Si ya hay un dispensado en curso, responde `409`:
```json
{ "ok": false, "busy": true }
```

### `GET /log`

Historial de eventos (más reciente primero).

```json
[
  { "timestamp": 482310, "grams": 13, "uid": "47 D6 0C 06" },
  { "timestamp": 120044, "grams": 44, "uid": "MANUAL" }
]
```

- `timestamp`: `millis()` desde el arranque del ESP32 (ver limitaciones)
- `uid`: UID del tag, o `"MANUAL"` si fue dispensado desde la app

### `POST /tare`

Pone la balanza en cero (plato vacío).

```json
{ "ok": true, "message": "tare" }
```

Si hay un dispensado en curso, responde `409` `{ "ok": false, "busy": true }`.

## Limitaciones y notas

- **Sin reloj de tiempo real:** el ESP32 no tiene RTC, por lo que `timestamp` en
  `/log` es `millis()` (tiempo desde el arranque). El frontend asigna la **hora del
  cliente** la primera vez que ve cada evento durante el sondeo; las fechas/horas y el
  filtro por fecha del historial son aproximados y válidos para la demo.
- **Inercia de la comida:** tras detener el motor, sigue cayendo algo de comida por
  inercia mecánica. Para compensarlo, el corte se hace en `meta - TOLERANCE_G`
  (`TOLERANCE_G = 2 g`), de modo que el peso final queda más cerca de la meta. Puede
  reajustarse según el hardware.
- **Orientación de la celda:** la lectura de peso se toma en **valor absoluto**
  (`fabs`) tanto en firmware como en frontend, así funciona sin importar el signo/
  orientación de la celda de carga.
- **Galería de fotos:** la sección "Fotos de los gatitos" aún no tiene imágenes reales
  (es un placeholder del diseño original).
- **LCD en bus compartido:** el LCD se re-inicializa en cada actualización para
  recuperarse de posible ruido eléctrico en el bus I2C (p. ej. del buzzer).

## Licencia / atribución

Proyecto académico del curso **CE5507 - Modelación Hardware Software con Orientación a
Objetos**, Tecnológico de Costa Rica (TEC), **I Semestre 2026**.
