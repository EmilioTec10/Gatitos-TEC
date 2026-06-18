/* ============================================================================
 *  Gatitos Tec - Integracion frontend <-> ESP32
 *  App web sin dependencias (vanilla JS, estructura OOP).
 *  Contrato con el firmware:
 *    GET  /status   -> { state, hopperRemaining, lastDispensed, plateWeight, wifi }
 *    POST /dispense -> { ok, grams, hopperRemaining }
 *    GET  /log      -> [ { timestamp, grams, uid }, ... ]
 * ========================================================================== */

/* ----------------------------- CONFIGURACION ----------------------------- */

// IP del ESP32 en tu red local. Cambia las X por la IP que imprime el ESP32
// por el monitor serie al conectarse al WiFi (ej. http://192.168.1.42).
const ESP32_URL = "http://10.49.8.203";

// Debe coincidir con HOPPER_CAPACITY del firmware (en gramos).
const HOPPER_CAPACITY_G = 1000;

// Cada cuanto se consulta /status (ms).
const POLL_INTERVAL_MS = 2000;

// Gramos por cada boton de porcion (data-portion 1..5). Debe coincidir con
// los #define PORTION_x del firmware.
const PORTION_GRAMS = { 1: 15, 2: 30, 3: 45, 4: 60, 5: 75 };

// Meta por defecto si no hay porcion seleccionada (= PORTION_3 del firmware).
const DEFAULT_PORTION_G = 45;

// Nombre del unico dispensador conectado.
const DISPENSER_NAME = "Soda El Lago";

// Mapa editable UID -> nombre del gato. Cambia los nombres aqui cuando quieras.
// Las claves deben coincidir EXACTO con el UID que reporta el firmware
// (hex en mayusculas, dos digitos por byte, separados por espacio).
const UID_TO_CAT = {
  "47 D6 0C 06": "Michi",   // llavero azul
  "02 8F 5C C5": "Pelusa"   // tag adhesivo
};

// Icono usado en cada entrada del historial.
const FEED_ICON_URL = "./icono%20control%20manual.png";

/* ------------------------- CLIENTE HTTP DEL ESP32 ------------------------ */
/**
 * Encapsula toda la comunicacion con el ESP32. Cada metodo devuelve una
 * promesa con el JSON parseado o lanza un error si la peticion falla.
 */
class ESP32Client {
  constructor(baseUrl) {
    this.baseUrl = baseUrl.replace(/\/+$/, ""); // sin slash final
  }

  async _request(path, { timeout = 4000, ...options } = {}) {
    // Timeout para que un ESP32 inalcanzable no deje la promesa colgada.
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);

    // Para GET agregamos un cache-buster (?_=ts): fuerza una conexion fresca y
    // evita reutilizar un socket keep-alive "envenenado" tras un fetch abortado.
    const method = (options.method || "GET").toUpperCase();
    let url = `${this.baseUrl}${path}`;
    if (method === "GET") {
      url += (url.includes("?") ? "&" : "?") + `_=${Date.now()}`;
    }

    try {
      const response = await fetch(url, {
        ...options,
        cache: "no-store",
        signal: controller.signal
      });
      if (!response.ok) {
        const error = new Error(`HTTP ${response.status}`);
        error.status = response.status;
        throw error;
      }
      return await response.json();
    } finally {
      clearTimeout(timer);
    }
  }

  getStatus() {
    return this._request("/status");
  }

  dispense(grams) {
    // Meta de peso opcional como query param (?grams=NN); enviarla asi (sin
    // cabeceras extra) evita el preflight CORS. El endpoint responde de
    // inmediato (202): solo ARRANCA el ciclo, no espera a que termine.
    const query = typeof grams === "number" ? `?grams=${grams}` : "";
    return this._request(`/dispense${query}`, { method: "POST", timeout: 5000 });
  }

  getLog() {
    return this._request("/log");
  }

  tare() {
    return this._request("/tare", { method: "POST" });
  }
}

/* ----------------------------- APP PRINCIPAL ----------------------------- */
/**
 * Controla la UI: navegacion, polling de estado, dispensado manual y
 * renderizado del historial a partir de los datos reales del ESP32.
 */
class DispenserApp {
  constructor(client) {
    this.client = client;

    // Estado interno
    this.connected = false;
    this.currentView = "home";
    this.selectedPortion = 0;
    this.isDateDescending = true;
    this.logEntries = [];          // historial ya mapeado para render
    this.seenEvents = new Map();   // timestamp(ESP32) -> Date (hora del cliente)
    this.lastLogSignature = "";    // para re-renderizar solo cuando cambia
    this.pollTimer = null;         // id del setTimeout del bucle de polling
    this.dispenseInFlight = false; // hay un dispensado manual en curso

    this.catsByDispenser = {
      [DISPENSER_NAME]: ["Michi", "Pelusa"]
    };

    this._cacheDom();
  }

  _cacheDom() {
    this.views = Array.from(document.querySelectorAll(".app-view"));
    this.navButtons = Array.from(document.querySelectorAll("[data-nav-target]"));

    this.feedingForm = document.querySelector("#feeding-form");
    this.dispenserSelect = document.querySelector("#dispenser-select");
    this.catSelect = document.querySelector("#cat-select");
    this.caretakerInput = document.querySelector("#caretaker-input");
    this.portionButtons = Array.from(document.querySelectorAll(".portion-pill"));
    this.portionTotal = document.querySelector("#portion-total");
    this.manualFeedButton = document.querySelector("#manual-feed");
    this.tareButton = document.querySelector("#tare-button");
    this.homeNote = document.querySelector("#home-note");
    this.photoUpload = document.querySelector("#photo-upload");
    this.uploadPreview = document.querySelector("#upload-preview");

    this.reserveValue = document.querySelector("#reserve-value");
    this.reserveFill = document.querySelector("#reserve-fill");
    this.reserveMax = document.querySelector("#reserve-max");
    this.reserveUpdateButton = document.querySelector("#reserve-update");
    this.reserveSettingsButton = document.querySelector("#reserve-settings");
    this.dispenserNote = document.querySelector("#dispensers-note");
    this.configureButtons = Array.from(document.querySelectorAll(".configure-dispenser"));
    this.dispenserCards = Array.from(document.querySelectorAll(".dispenser-card"));

    this.dispenserStatus = document.querySelector("#dispenser-status");
    this.hopperLevel = document.querySelector("#hopper-level");
    this.hopperFill = document.querySelector("#hopper-fill");
    this.hopperPct = document.querySelector("#hopper-pct");
    this.lastDispensedEl = document.querySelector("#last-dispensed");
    this.plateWeightEl = document.querySelector("#plate-weight");
    this.todayCount = document.querySelector("#today-count");

    this.historyGroups = document.querySelector("#history-groups");
    this.dispenserFilter = document.querySelector("#filter-dispenser");
    this.typeFilter = document.querySelector("#filter-type");
    this.dateSortButton = document.querySelector("#date-sort");
    this.historyNote = document.querySelector("#history-note");
  }

  /* --------------------------- ARRANQUE --------------------------- */
  init() {
    this._bindEvents();
    this.reserveMax.textContent = `${(HOPPER_CAPACITY_G / 1000).toFixed(2)} kg max.`;

    // Preselecciona el unico dispensador y carga sus gatos
    this.dispenserSelect.value = DISPENSER_NAME;
    this.populateCats(DISPENSER_NAME);

    this.updatePortionTotal();
    this.updateFeedControls();

    // Carga inicial del historial y arranque del bucle de polling
    this.refreshLog();
    this.startPolling();
  }

  /* --------------------------- BUCLE DE POLLING --------------------------- */
  /**
   * Bucle auto-reprogramable: cada ciclo agenda el siguiente dentro de un
   * `finally`, de modo que un fetch fallido (o lento, ej. el ESP32 ocupado
   * ~5s durante un dispensado) NUNCA detiene el polling. Ademas, al esperar
   * a que termine el poll actual antes de agendar el proximo, evita que se
   * acumulen peticiones solapadas mientras el ESP32 esta ocupado.
   * En el siguiente poll exitoso, poll() vuelve a marcar "Conectado".
   */
  startPolling() {
    const tick = async () => {
      try {
        // Mientras un dispensado manual esta en curso, manualFeed() lleva el
        // control del estado; el bucle de fondo se salta su poll para no
        // duplicar peticiones contra el ESP32 (de un solo cliente).
        if (!this.dispenseInFlight) {
          await this.poll();
        }
      } finally {
        this.pollTimer = setTimeout(tick, POLL_INTERVAL_MS);
      }
    };
    tick();
  }

  _bindEvents() {
    this.navButtons.forEach((button) => {
      button.addEventListener("click", () => this.setView(button.dataset.navTarget));
    });

    this.dispenserSelect.addEventListener("change", () => {
      this.populateCats(this.dispenserSelect.value);
      this.catSelect.value = "";
    });

    this.portionButtons.forEach((button) => {
      button.addEventListener("click", () => {
        this.selectedPortion = Number(button.dataset.portion);
        this.portionButtons.forEach((pill) => {
          const isSelected = pill === button;
          pill.classList.toggle("is-selected", isSelected);
          pill.setAttribute("aria-pressed", String(isSelected));
        });
        this.updatePortionTotal();
      });
    });

    // Alimentar Manual -> POST /dispense (los campos del formulario son opcionales)
    this.feedingForm.addEventListener("submit", (event) => {
      event.preventDefault();
      this.manualFeed();
    });

    this.tareButton.addEventListener("click", () => this.tare());

    this.photoUpload.addEventListener("change", () => this.handlePhotoUpload());

    // "Actualizar ahora" fuerza un refresco inmediato del estado
    this.reserveUpdateButton.addEventListener("click", () => {
      this.setDispenserNote("Actualizando estado del dispensador...");
      this.poll();
      this.refreshLog();
    });

    this.reserveSettingsButton.addEventListener("click", () => {
      this.setDispenserNote("Configuracion de reserva general (proximamente).");
    });

    this.configureButtons.forEach((button) => {
      button.addEventListener("click", () => {
        const card = button.closest(".dispenser-card");
        if (!card) return;
        const name = card.dataset.dispenser;
        this.dispenserCards.forEach((item) => item.classList.toggle("is-focused", item === card));
        this.dispenserSelect.value = name;
        this.populateCats(name);
        this.catSelect.value = "";
        this.setDispenserNote(`Usando ${name} como contexto para el control manual.`);
        this.setHomeNote(`Listo para alimentar ${name}.`);
        this.setView("home");
      });
    });

    [this.dispenserFilter, this.typeFilter].forEach((control) => {
      control.addEventListener("change", () => this.renderHistory());
    });

    this.dateSortButton.addEventListener("click", () => {
      this.isDateDescending = !this.isDateDescending;
      this.dateSortButton.setAttribute("aria-pressed", String(!this.isDateDescending));
      this.renderHistory();
    });
  }

  /* --------------------------- NAVEGACION --------------------------- */
  setView(viewName) {
    this.currentView = viewName;
    this.views.forEach((view) => view.classList.toggle("is-active", view.dataset.view === viewName));
    this.navButtons.forEach((button) =>
      button.classList.toggle("nav-link--active", button.dataset.navTarget === viewName)
    );
  }

  setHomeNote(message) { this.homeNote.textContent = message; }
  setDispenserNote(message) { this.dispenserNote.textContent = message; }
  setHistoryNote(message) { this.historyNote.textContent = message; }

  /* --------------------------- FORMULARIO --------------------------- */
  populateCats(dispenserName) {
    const cats = this.catsByDispenser[dispenserName] ?? [];
    this.catSelect.innerHTML = '<option value="">Elige un gato</option>';
    cats.forEach((cat) => {
      const option = document.createElement("option");
      option.value = cat;
      option.textContent = cat;
      this.catSelect.appendChild(option);
    });
    this.catSelect.disabled = !cats.length;
  }

  updatePortionTotal() {
    const grams = PORTION_GRAMS[this.selectedPortion] || 0;
    this.portionTotal.textContent = `${grams}g`;
  }

  // Los botones de accion solo dependen de que el ESP32 este conectado.
  updateFeedControls() {
    this.manualFeedButton.disabled = !this.connected;
    this.tareButton.disabled = !this.connected;
  }

  handlePhotoUpload() {
    this.uploadPreview.innerHTML = "";
    const files = Array.from(this.photoUpload.files || []);
    if (!files.length) {
      this.setHomeNote("No se seleccionaron imagenes.");
      return;
    }
    files.slice(0, 3).forEach((file) => {
      const item = document.createElement("li");
      item.textContent = `${file.name} · ${(file.size / 1024).toFixed(0)} KB`;
      this.uploadPreview.appendChild(item);
    });
    this.setHomeNote(`${files.length} imagen(es) listas para revisar.`);
  }

  /* --------------------------- DISPENSADO MANUAL --------------------------- */
  async manualFeed() {
    if (!this.connected) {
      this.setHomeNote("El dispensador no esta conectado.");
      return;
    }
    // Meta = gramos de la porcion seleccionada, o el valor por defecto (45g)
    const target = PORTION_GRAMS[this.selectedPortion] || DEFAULT_PORTION_G;

    // Pausa el bucle de fondo: este metodo lleva el control mientras dispensa.
    this.dispenseInFlight = true;
    this.manualFeedButton.disabled = true;
    this.setHomeNote(`Dispensando ${target}g...`);
    try {
      // El endpoint responde de inmediato (202): solo arranca el ciclo.
      await this.client.dispense(target);

      // Espera a que el ESP32 termine: sondea /status hasta que el estado
      // deje de ser DISPENSING (con tope de seguridad).
      const finalStatus = await this.waitForIdle(15000);
      const grams = finalStatus ? Number(finalStatus.lastDispensed) || 0 : 0;
      this.setHomeNote(`Alimentacion manual lista: ${grams}g dispensados.`);

      await this.refreshLog();
      this.setView("history");
    } catch (error) {
      // 409 = ya hay un dispensado en curso; cualquier otro = inalcanzable
      if (error && error.status === 409) {
        this.setHomeNote("El dispensador esta ocupado, intenta de nuevo.");
      } else {
        this.setHomeNote("No se pudo dispensar (ESP32 inalcanzable).");
      }
    } finally {
      this.dispenseInFlight = false;
      this.updateFeedControls();
    }
  }

  // Sondea /status hasta que el ciclo termine (state != DISPENSING) o se agote
  // el tope de tiempo. Devuelve el ultimo /status recibido (o null si fallo).
  async waitForIdle(maxMs) {
    const start = Date.now();
    let lastData = null;
    while (Date.now() - start < maxMs) {
      try {
        const data = await this.client.getStatus();
        this.connected = true;
        this.updateStatusUI(true, data);
        lastData = data;
        if (data.state !== "DISPENSING") break;   // ciclo terminado
      } catch (error) {
        // Reintento transitorio (ej. el ESP32 ocupado un instante)
        this.connected = false;
        this.updateStatusUI(false, null);
      }
      await new Promise((resolve) => setTimeout(resolve, 500));
    }
    return lastData;
  }

  /* --------------------------- TARA DE LA BALANZA --------------------------- */
  async tare() {
    if (!this.connected) {
      this.setHomeNote("El dispensador no esta conectado.");
      return;
    }
    this.tareButton.disabled = true;
    this.setHomeNote("Calibrando cero de la balanza...");
    try {
      await this.client.tare();
      this.setHomeNote("Tara realizada: balanza puesta en cero.");
      await this.poll();
    } catch (error) {
      // 409 = el ESP32 esta dispensando; cualquier otro = inalcanzable
      if (error && error.status === 409) {
        this.setHomeNote("El dispensador esta ocupado, intenta la tara luego.");
      } else {
        this.setHomeNote("No se pudo hacer la tara (ESP32 inalcanzable).");
      }
    } finally {
      this.updateFeedControls();
    }
  }

  /* --------------------------- POLLING DE ESTADO --------------------------- */
  async poll() {
    try {
      const data = await this.client.getStatus();
      this.connected = true;
      this.updateStatusUI(true, data);
      // Refresca el historial para reflejar dispensados autonomos (NFC).
      // refreshLog ignora la respuesta si no hubo cambios.
      this.refreshLog();
    } catch (error) {
      this.connected = false;
      this.updateStatusUI(false, null);
    }
    this.updateFeedControls();
  }

  updateStatusUI(connected, data) {
    if (!connected || !data) {
      // ESP32 inalcanzable: marca desconectado sin romper la UI
      this.dispenserStatus.className = "status-badge status-badge--offline";
      this.dispenserStatus.innerHTML =
        '<img src="./icono%20desconectado.png" alt="" aria-hidden="true" />Desconectado';
      this.hopperLevel.textContent = "--";
      this.hopperPct.textContent = "-- de capacidad";
      this.hopperFill.style.width = "0%";
      this.lastDispensedEl.textContent = "--";
      this.plateWeightEl.textContent = "--";
      this.reserveValue.textContent = "--";
      this.reserveFill.style.width = "0%";
      if (this.currentView !== "history") {
        this.setHomeNote("Dispensador desconectado. Revisa la IP y el WiFi del ESP32.");
      }
      return;
    }

    // Conectado
    this.dispenserStatus.className = "status-badge status-badge--online";
    this.dispenserStatus.textContent = "Conectado";

    const hopperG = Number(data.hopperRemaining) || 0;
    const pct = Math.max(0, Math.min(100, (hopperG / HOPPER_CAPACITY_G) * 100));

    this.hopperLevel.textContent = `${(hopperG / 1000).toFixed(2)} kg`;
    this.hopperFill.style.width = `${pct}%`;
    this.hopperPct.textContent = `${Math.round(pct)}% de capacidad`;

    this.lastDispensedEl.textContent = `${Number(data.lastDispensed) || 0} g`;
    this.plateWeightEl.textContent = `${Number(data.plateWeight) || 0} g`;

    this.reserveValue.textContent = (hopperG / 1000).toFixed(2);
    this.reserveFill.style.width = `${pct}%`;

    if (this.currentView === "home" && this.homeNote.textContent.startsWith("Conectando")) {
      this.setHomeNote("Dispensador conectado. Listo para alimentar.");
    }
  }

  /* --------------------------- HISTORIAL (/log) --------------------------- */
  async refreshLog() {
    try {
      const raw = await this.client.getLog();
      const signature = raw.map((e) => `${e.timestamp}:${e.grams}`).join("|");
      if (signature === this.lastLogSignature) return; // sin cambios
      this.lastLogSignature = signature;
      this.logEntries = raw.map((entry) => this.mapLogEntry(entry));
      this.renderHistory();
    } catch (error) {
      // ESP32 inalcanzable: no rompemos el historial ya mostrado
      this.setHistoryNote("No se pudo actualizar el historial (ESP32 inalcanzable).");
    }
  }

  // Convierte un evento crudo del firmware al formato que usa el render.
  mapLogEntry(entry) {
    const uid = entry.uid ?? "";
    const isManual = uid === "MANUAL";
    const cat = UID_TO_CAT[uid] || (isManual ? "Manual" : uid);

    // El firmware reporta millis() (no hora real). Asignamos la hora del
    // cliente la primera vez que vemos cada evento durante el polling.
    if (!this.seenEvents.has(entry.timestamp)) {
      this.seenEvents.set(entry.timestamp, new Date());
    }
    const when = this.seenEvents.get(entry.timestamp);

    return {
      date: when.toISOString().slice(0, 10),
      time: when.toTimeString().slice(0, 5),
      cat,
      dispenser: DISPENSER_NAME,
      grams: Number(entry.grams) || 0,
      type: isManual ? "manual" : "sensor"
    };
  }

  groupByDate(entries) {
    return entries.reduce((groups, entry) => {
      const bucket = groups.get(entry.date) ?? [];
      bucket.push(entry);
      groups.set(entry.date, bucket);
      return groups;
    }, new Map());
  }

  createEntryMarkup(entry) {
    return `
      <li class="history-entry">
        <div class="history-entry__main">
          <div class="history-entry__icon">
            <img src="${FEED_ICON_URL}" alt="" aria-hidden="true" />
          </div>
          <div class="history-entry__text">
            <strong>${entry.cat}</strong>
            <span>${entry.dispenser} · ${entry.time}</span>
          </div>
        </div>
        <div class="history-entry__grams">${entry.grams}g</div>
      </li>
    `;
  }

  getFilteredEntries() {
    return this.logEntries.filter((entry) => {
      const matchesDispenser =
        this.dispenserFilter.value === "all" || entry.dispenser === this.dispenserFilter.value;
      const matchesType = this.typeFilter.value === "all" || entry.type === this.typeFilter.value;
      return matchesDispenser && matchesType;
    });
  }

  renderHistory() {
    // Contador "Hoy" del dispensador
    const today = new Date().toISOString().slice(0, 10);
    const todayTotal = this.logEntries.filter((e) => e.date === today).length;
    this.todayCount.textContent = `${todayTotal} ${todayTotal === 1 ? "vez" : "veces"}`;

    const entries = this.getFilteredEntries();

    if (!entries.length) {
      this.historyGroups.innerHTML =
        '<div class="empty-state">No hay registros para los filtros seleccionados.</div>';
      this.setHistoryNote("No hay resultados para la combinacion actual de filtros.");
      return;
    }

    const sortedGroups = Array.from(this.groupByDate(entries).entries()).sort((left, right) =>
      this.isDateDescending ? right[0].localeCompare(left[0]) : left[0].localeCompare(right[0])
    );

    this.historyGroups.innerHTML = sortedGroups
      .map(
        ([date, items]) => `
        <article class="history-day-card">
          <h3 class="history-day-title">${date}</h3>
          <ul class="history-day-list">${items.map((item) => this.createEntryMarkup(item)).join("")}</ul>
        </article>
      `
      )
      .join("");

    const fragments = [];
    if (this.dispenserFilter.value !== "all") fragments.push(`dispensador ${this.dispenserFilter.value}`);
    if (this.typeFilter.value !== "all") {
      fragments.push(`tipo ${this.typeFilter.options[this.typeFilter.selectedIndex].text}`);
    }
    fragments.push(this.isDateDescending ? "fecha descendente" : "fecha ascendente");
    this.setHistoryNote(`Mostrando ${entries.length} registro(s) para ${fragments.join(", ")}.`);
  }
}

/* ------------------------------- BOOTSTRAP ------------------------------- */
const app = new DispenserApp(new ESP32Client(ESP32_URL));
app.init();
