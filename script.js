const views = Array.from(document.querySelectorAll('.app-view'));
const navButtons = Array.from(document.querySelectorAll('[data-nav-target]'));

const feedingForm = document.querySelector('#feeding-form');
const dispenserSelect = document.querySelector('#dispenser-select');
const catSelect = document.querySelector('#cat-select');
const caretakerInput = document.querySelector('#caretaker-input');
const portionButtons = Array.from(document.querySelectorAll('.portion-pill'));
const portionTotal = document.querySelector('#portion-total');
const manualFeedButton = document.querySelector('#manual-feed');
const sensorFeedButton = document.querySelector('#sensor-feed');
const homeNote = document.querySelector('#home-note');
const photoUpload = document.querySelector('#photo-upload');
const uploadPreview = document.querySelector('#upload-preview');

const reserveValue = document.querySelector('#reserve-value');
const reserveFill = document.querySelector('#reserve-fill');
const reserveUpdateButton = document.querySelector('#reserve-update');
const reserveSettingsButton = document.querySelector('#reserve-settings');
const dispenserNote = document.querySelector('#dispensers-note');
const configureButtons = Array.from(document.querySelectorAll('.configure-dispenser'));
const dispenserCards = Array.from(document.querySelectorAll('.dispenser-card'));

const historyGroups = document.querySelector('#history-groups');
const dispenserFilter = document.querySelector('#filter-dispenser');
const typeFilter = document.querySelector('#filter-type');
const dateSortButton = document.querySelector('#date-sort');
const historyNote = document.querySelector('#history-note');

const catsByDispenser = {
  'Soda El Lago': ['Michi', 'Luna'],
  Forestal: ['Pelusa', 'Tigre'],
  D3: ['Nala', 'Simba']
};

const historyEntries = [
  { date: '2026-06-05', cat: 'Michi', dispenser: 'Soda El Lago', time: '08:15', grams: 45, type: 'manual' },
  { date: '2026-06-05', cat: 'Luna', dispenser: 'Soda El Lago', time: '08:18', grams: 50, type: 'sensor' },
  { date: '2026-06-05', cat: 'Pelusa', dispenser: 'Forestal', time: '14:30', grams: 48, type: 'manual' },
  { date: '2026-06-05', cat: 'Nala', dispenser: 'D3', time: '18:05', grams: 50, type: 'sensor' },
  { date: '2026-06-04', cat: 'Tigre', dispenser: 'Forestal', time: '08:12', grams: 47, type: 'manual' }
];

const feedIconUrl = 'http://localhost:3845/assets/19fc27ccd2e8a147b046dd2a2c9035215894ad2a.svg';
let currentView = 'home';
let selectedPortion = 0;
let reserveKg = 25;
let isDateDescending = true;

function setView(viewName) {
  currentView = viewName;
  views.forEach((view) => {
    view.classList.toggle('is-active', view.dataset.view === viewName);
  });

  navButtons.forEach((button) => {
    button.classList.toggle('nav-link--active', button.dataset.navTarget === viewName);
  });
}

function setHomeNote(message) {
  homeNote.textContent = message;
}

function setDispenserNote(message) {
  dispenserNote.textContent = message;
}

function setHistoryNote(message) {
  historyNote.textContent = message;
}

function populateCats(dispenserName) {
  const cats = catsByDispenser[dispenserName] ?? [];
  catSelect.innerHTML = '<option value="">Elige un gato</option>';
  cats.forEach((cat) => {
    const option = document.createElement('option');
    option.value = cat;
    option.textContent = cat;
    catSelect.appendChild(option);
  });
  catSelect.disabled = !cats.length;
}

function updatePortionTotal() {
  portionTotal.textContent = `${selectedPortion * 10}g`;
}

function isFeedReady() {
  return Boolean(dispenserSelect.value && catSelect.value && caretakerInput.value.trim() && selectedPortion);
}

function updateFeedControls() {
  const enabled = isFeedReady();
  manualFeedButton.disabled = !enabled;
  sensorFeedButton.disabled = !enabled;
}

function groupByDate(entries) {
  return entries.reduce((groups, entry) => {
    const bucket = groups.get(entry.date) ?? [];
    bucket.push(entry);
    groups.set(entry.date, bucket);
    return groups;
  }, new Map());
}

function createEntryMarkup(entry) {
  return `
    <li class="history-entry">
      <div class="history-entry__main">
        <div class="history-entry__icon">
          <img src="${feedIconUrl}" alt="" aria-hidden="true" />
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

function getFilteredEntries() {
  return historyEntries.filter((entry) => {
    const matchesDispenser = dispenserFilter.value === 'all' || entry.dispenser === dispenserFilter.value;
    const matchesType = typeFilter.value === 'all' || entry.type === typeFilter.value;
    return matchesDispenser && matchesType;
  });
}

function renderHistory() {
  const entries = getFilteredEntries();

  if (!entries.length) {
    historyGroups.innerHTML = '<div class="empty-state">No hay registros para los filtros seleccionados.</div>';
    setHistoryNote('No hay resultados para la combinacion actual de filtros.');
    return;
  }

  const sortedGroups = Array.from(groupByDate(entries).entries()).sort((left, right) => {
    if (isDateDescending) {
      return right[0].localeCompare(left[0]);
    }
    return left[0].localeCompare(right[0]);
  });

  historyGroups.innerHTML = sortedGroups
    .map(([date, items]) => `
      <article class="history-day-card">
        <h3 class="history-day-title">${date}</h3>
        <ul class="history-day-list">${items.map(createEntryMarkup).join('')}</ul>
      </article>
    `)
    .join('');

  const fragments = [];
  if (dispenserFilter.value !== 'all') {
    fragments.push(`dispensador ${dispenserFilter.value}`);
  }
  if (typeFilter.value !== 'all') {
    fragments.push(`tipo ${typeFilter.options[typeFilter.selectedIndex].text}`);
  }
  fragments.push(isDateDescending ? 'fecha descendente' : 'fecha ascendente');
  setHistoryNote(`Mostrando ${entries.length} registro(s) para ${fragments.join(', ')}.`);
}

function updateReserveDisplay() {
  reserveValue.textContent = reserveKg.toFixed(1);
  reserveFill.style.width = `${(reserveKg / 50) * 100}%`;
}

function addHistoryEntry(type) {
  historyEntries.unshift({
    date: '2026-06-15',
    cat: catSelect.value,
    dispenser: dispenserSelect.value,
    time: type === 'manual' ? '19:12' : '19:13',
    grams: selectedPortion * 10,
    type
  });
  renderHistory();
}

navButtons.forEach((button) => {
  button.addEventListener('click', () => {
    setView(button.dataset.navTarget);
  });
});

dispenserSelect.addEventListener('change', () => {
  populateCats(dispenserSelect.value);
  catSelect.value = '';
  updateFeedControls();

  if (!dispenserSelect.value) {
    setHomeNote('Selecciona un dispensador para habilitar los gatos disponibles.');
    return;
  }

  setHomeNote(`Dispensador ${dispenserSelect.value} listo para configurar la alimentacion.`);
});

catSelect.addEventListener('change', updateFeedControls);
caretakerInput.addEventListener('input', updateFeedControls);

portionButtons.forEach((button) => {
  button.addEventListener('click', () => {
    selectedPortion = Number(button.dataset.portion);
    portionButtons.forEach((pill) => {
      const isSelected = pill === button;
      pill.classList.toggle('is-selected', isSelected);
      pill.setAttribute('aria-pressed', String(isSelected));
    });
    updatePortionTotal();
    updateFeedControls();
  });
});

feedingForm.addEventListener('submit', (event) => {
  event.preventDefault();
  if (!isFeedReady()) {
    setHomeNote('Completa todos los campos antes de alimentar manualmente.');
    return;
  }

  addHistoryEntry('manual');
  setHomeNote(`Alimentacion manual registrada para ${catSelect.value} en ${dispenserSelect.value}.`);
  setView('history');
});

sensorFeedButton.addEventListener('click', () => {
  if (!isFeedReady()) {
    setHomeNote('Completa todos los campos antes de simular el sensor.');
    return;
  }

  addHistoryEntry('sensor');
  setHomeNote(`Simulacion de sensor registrada para ${catSelect.value} en ${dispenserSelect.value}.`);
  setView('history');
});

photoUpload.addEventListener('change', () => {
  uploadPreview.innerHTML = '';
  const files = Array.from(photoUpload.files || []);
  if (!files.length) {
    setHomeNote('No se seleccionaron imagenes.');
    return;
  }

  files.slice(0, 3).forEach((file) => {
    const item = document.createElement('li');
    item.textContent = `${file.name} · ${(file.size / 1024).toFixed(0)} KB`;
    uploadPreview.appendChild(item);
  });

  setHomeNote(`${files.length} imagen(es) listas para revisar.`);
});

reserveUpdateButton.addEventListener('click', () => {
  const nextValue = window.prompt('Nueva cantidad en reserva (0 a 50 kg):', reserveKg.toFixed(1));
  if (nextValue === null) {
    setDispenserNote('Actualizacion de reserva cancelada.');
    return;
  }

  const parsedValue = Number(nextValue);
  if (Number.isNaN(parsedValue) || parsedValue < 0 || parsedValue > 50) {
    setDispenserNote('Ingresa un valor valido entre 0 y 50 kg.');
    return;
  }

  reserveKg = parsedValue;
  updateReserveDisplay();
  setDispenserNote(`Reserva general actualizada a ${reserveKg.toFixed(1)} kg.`);
});

reserveSettingsButton.addEventListener('click', () => {
  setDispenserNote('Configuracion de reserva general abierta.');
});

configureButtons.forEach((button) => {
  button.addEventListener('click', () => {
    const card = button.closest('.dispenser-card');
    if (!card) {
      return;
    }

    const dispenserName = card.dataset.dispenser;
    dispenserCards.forEach((item) => item.classList.toggle('is-focused', item === card));
    dispenserSelect.value = dispenserName;
    populateCats(dispenserName);
    catSelect.value = '';
    updateFeedControls();
    setDispenserNote(`Usando ${dispenserName} como contexto para el control manual.`);
    setHomeNote(`Completa la alimentacion para ${dispenserName}.`);
    setView('home');
  });
});

[dispenserFilter, typeFilter].forEach((control) => {
  control.addEventListener('change', renderHistory);
});

dateSortButton.addEventListener('click', () => {
  isDateDescending = !isDateDescending;
  dateSortButton.setAttribute('aria-pressed', String(!isDateDescending));
  renderHistory();
});

updatePortionTotal();
updateFeedControls();
updateReserveDisplay();
renderHistory();
