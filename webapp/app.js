/**
 * ==========================================================================
 * SMART IRRIGATION SYSTEM - PWA FRONTEND APPLICATION LOGIC (app.js)
 * High-Performance, Standalone Vanilla JavaScript IoT Controller Interface
 * ==========================================================================
 * 
 * CORS NOTICE FOR ESP8266 FIRMWARE:
 * When this PWA is hosted on a local web server (e.g. laptop python -m http.server 8000)
 * and connects directly to the ESP8266 IP (e.g. http://192.168.1.50 or http://smart-irrigation.local),
 * the ESP8266 Web Server firmware MUST send appropriate CORS headers in its REST API responses:
 *   Access-Control-Allow-Origin: *
 *   Access-Control-Allow-Methods: GET, POST, OPTIONS
 *   Access-Control-Allow-Headers: Content-Type
 * 
 * ==========================================================================
 */

'use strict';

// ==========================================================================
// 1. CENTRALIZED APPLICATION STATE
// ==========================================================================
const state = {
  // Connection Target (Persisted in localStorage)
  baseUrl: 'http://smart-irrigation.local',
  
  // Connection State: 'connecting' | 'connected' | 'disconnected'
  connectionStatus: 'disconnected',
  
  // Timestamps
  lastUpdated: null,
  
  // Polling Control
  pollIntervalId: null,
  isPollingInProgress: false,
  isRequestPending: false, // Prevents duplicate API requests during user actions

  // PWA Install Deferred Prompt
  deferredInstallPrompt: null,

  // Device Telemetry (GET /api/status)
  deviceData: {
    device: '--',
    version: '--',
    ip: '--',
    uptime: 0,
    wifi: false,
    mode: 'auto',       // 'auto' | 'manual'
    pump: false,        // boolean
    soilMoisture: 0,    // percentage
    temperature: 0,     // Celsius
    humidity: 0,        // percentage
    sensorError: false  // boolean
  },

  // Device Configuration (GET /api/config)
  configData: {
    startThreshold: 35,
    stopThreshold: 45,
    maxRuntimeMs: 30000
  }
};

// DOM Element Cache
const DOM = {};

// ==========================================================================
// 2. INITIALIZATION ROUTINE
// ==========================================================================
document.addEventListener('DOMContentLoaded', () => {
  cacheDOMElements();
  loadSavedBaseUrl();
  setupEventListeners();
  registerServiceWorker();
  initPWAInstallPrompt();

  // Initial Data Synchronization
  setConnectionStatus('connecting');
  initialFetchSequence();

  // Start 3-second live polling loop
  startPolling();
});

function cacheDOMElements() {
  DOM.connectionBadge = document.getElementById('connection-status-badge');
  DOM.connectionText = document.getElementById('connection-status-text');
  DOM.lastUpdatedDisplay = document.getElementById('last-updated-display');
  DOM.btnInstallPWA = document.getElementById('btn-install-pwa');
  DOM.sensorErrorBanner = document.getElementById('sensor-error-banner');

  // Connection Setup
  DOM.inputEspUrl = document.getElementById('input-esp-url');
  DOM.btnConnect = document.getElementById('btn-connect');
  DOM.btnConnectText = DOM.btnConnect.querySelector('.btn-text');
  DOM.btnConnectSpinner = DOM.btnConnect.querySelector('.btn-spinner');

  // Telemetry Dashboard
  DOM.displayMoistureVal = document.getElementById('display-moisture-val');
  DOM.gaugeRingFill = document.getElementById('gauge-ring-fill');
  DOM.moistureStatusTag = document.getElementById('moisture-status-tag');
  DOM.displayTempVal = document.getElementById('display-temp-val');
  DOM.displayHumidityVal = document.getElementById('display-humidity-val');

  // Pump Control
  DOM.pumpStatusBadge = document.getElementById('pump-status-badge');
  DOM.pumpStatusText = document.getElementById('pump-status-text');
  DOM.btnTogglePump = document.getElementById('btn-toggle-pump');
  DOM.pumpBtnLabel = document.getElementById('pump-btn-label');
  DOM.pumpBtnSpinner = document.getElementById('pump-btn-spinner');
  DOM.pumpBtnIcon = document.getElementById('pump-btn-icon');
  DOM.pumpAutoNotice = document.getElementById('pump-auto-notice');

  // Mode Control
  DOM.modeStatusBadge = document.getElementById('mode-status-badge');
  DOM.btnModeAuto = document.getElementById('btn-mode-auto');
  DOM.btnModeManual = document.getElementById('btn-mode-manual');
  DOM.modeDescText = document.getElementById('mode-desc-text');

  // Configuration Form
  DOM.configForm = document.getElementById('config-form');
  DOM.inputStartThreshold = document.getElementById('input-start-threshold');
  DOM.inputStopThreshold = document.getElementById('input-stop-threshold');
  DOM.inputMaxRuntime = document.getElementById('input-max-runtime');
  DOM.configFeedback = document.getElementById('config-feedback');
  DOM.btnSaveConfig = document.getElementById('btn-save-config');
  DOM.btnSaveConfigText = DOM.btnSaveConfig.querySelector('.btn-text');
  DOM.btnSaveConfigSpinner = DOM.btnSaveConfig.querySelector('.btn-spinner');

  // Device Info
  DOM.infoDeviceName = document.getElementById('info-device-name');
  DOM.infoFirmwareVersion = document.getElementById('info-firmware-version');
  DOM.infoIpAddress = document.getElementById('info-ip-address');
  DOM.infoUptime = document.getElementById('info-uptime');
  DOM.infoWifiStatus = document.getElementById('info-wifi-status');

  // Toast Container
  DOM.toastContainer = document.getElementById('toast-container');
}

// ==========================================================================
// 3. BASE URL HANDLING & LOCALSTORAGE
// ==========================================================================
function loadSavedBaseUrl() {
  const savedUrl = localStorage.getItem('esp_base_url');
  if (savedUrl && savedUrl.trim() !== '') {
    state.baseUrl = savedUrl.trim();
  } else {
    state.baseUrl = 'http://smart-irrigation.local';
  }
  if (DOM.inputEspUrl) {
    DOM.inputEspUrl.value = state.baseUrl;
  }
}

function saveBaseUrl(newUrl) {
  const normalized = normalizeUrl(newUrl);
  state.baseUrl = normalized;
  localStorage.setItem('esp_base_url', normalized);
  if (DOM.inputEspUrl) {
    DOM.inputEspUrl.value = normalized;
  }
  return normalized;
}

function normalizeUrl(url) {
  if (!url || typeof url !== 'string') return 'http://smart-irrigation.local';
  let cleaned = url.trim();
  if (!cleaned.startsWith('http://') && !cleaned.startsWith('https://')) {
    cleaned = 'http://' + cleaned;
  }
  // Strip trailing slashes to prevent double slashes in API paths
  return cleaned.replace(/\/+$/, '');
}

function getBaseUrl() {
  return normalizeUrl(state.baseUrl);
}

function buildApiUrl(endpoint) {
  const base = getBaseUrl();
  const path = endpoint.startsWith('/') ? endpoint : '/' + endpoint;
  return `${base}${path}`;
}

// ==========================================================================
// 4. API COMMUNICATION HELPER (WITH TIMEOUT & ERROR HANDLING)
// ==========================================================================
async function apiRequest(endpoint, options = {}, timeoutMs = 3000) {
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

  const fetchConfig = {
    ...options,
    signal: controller.signal,
    headers: {
      'Content-Type': 'application/json',
      'Accept': 'application/json',
      ...(options.headers || {})
    }
  };

  const fullUrl = buildApiUrl(endpoint);

  try {
    const response = await fetch(fullUrl, fetchConfig);
    clearTimeout(timeoutId);

    if (!response.ok) {
      throw new Error(`HTTP ${response.status} (${response.statusText})`);
    }

    const data = await response.json();
    return data;
  } catch (error) {
    clearTimeout(timeoutId);
    if (error.name === 'AbortError') {
      throw new Error(`Connection timeout (${timeoutMs}ms) to ${fullUrl}`);
    }
    if (error instanceof TypeError && error.message.includes('Failed to fetch')) {
      throw new Error(`Unable to reach ESP8266 controller. Verify Wi-Fi network and device power.`);
    }
    throw error;
  }
}

// ==========================================================================
// 5. FETCHING TELEMETRY & CONFIGURATION
// ==========================================================================
async function initialFetchSequence() {
  try {
    await Promise.all([fetchStatus(), fetchConfig()]);
  } catch (err) {
    console.warn('[Init] Initial data fetch warning:', err.message);
  }
}

async function fetchStatus() {
  try {
    const data = await apiRequest('/api/status', { method: 'GET' }, 3000);

    // Validate expected payload structure
    if (!data || typeof data !== 'object') {
      throw new Error('Invalid JSON status payload');
    }

    // Synchronize application state with device payload
    state.deviceData = {
      device: data.device || 'Smart Irrigation',
      version: data.version || '1.0.0',
      ip: data.ip || '--',
      uptime: typeof data.uptime === 'number' ? data.uptime : 0,
      wifi: Boolean(data.wifi),
      mode: data.mode === 'manual' ? 'manual' : 'auto',
      pump: Boolean(data.pump),
      soilMoisture: typeof data.soilMoisture === 'number' ? data.soilMoisture : 0,
      temperature: typeof data.temperature === 'number' ? data.temperature : 0,
      humidity: typeof data.humidity === 'number' ? data.humidity : 0,
      sensorError: Boolean(data.sensorError)
    };

    state.lastUpdated = new Date();
    setConnectionStatus('connected');
    renderAllUI();
    return true;
  } catch (error) {
    console.error('[Status Poll Error]', error.message);
    setConnectionStatus('disconnected');
    renderLastUpdatedText();
    return false;
  }
}

async function fetchConfig() {
  try {
    const data = await apiRequest('/api/config', { method: 'GET' }, 3000);

    if (!data || typeof data !== 'object') {
      throw new Error('Invalid JSON config payload');
    }

    state.configData = {
      startThreshold: typeof data.startThreshold === 'number' ? data.startThreshold : 35,
      stopThreshold: typeof data.stopThreshold === 'number' ? data.stopThreshold : 45,
      maxRuntimeMs: typeof data.maxRuntimeMs === 'number' ? data.maxRuntimeMs : 30000
    };

    renderConfigFormFields();
    return true;
  } catch (error) {
    console.error('[Config Fetch Error]', error.message);
    return false;
  }
}

// ==========================================================================
// 6. LIVE POLLING MANAGEMENT (3-SECOND LOOP)
// ==========================================================================
function startPolling() {
  if (state.pollIntervalId) {
    clearInterval(state.pollIntervalId);
  }

  // Poll GET /api/status every 3 seconds (3000ms)
  state.pollIntervalId = setInterval(async () => {
    // Avoid starting poll cycle if user action is currently executing
    if (state.isRequestPending || state.isPollingInProgress) {
      return;
    }

    state.isPollingInProgress = true;
    await fetchStatus();
    state.isPollingInProgress = false;
  }, 3000);
}

// ==========================================================================
// 7. PUMP CONTROL ACTION (AUTO -> MANUAL SWAP REQUIREMENT)
// ==========================================================================
/**
 * IMPORTANT REQUIREMENT 22:
 * When the user manually turns the pump ON or OFF:
 * If the current mode is AUTO, the application MUST automatically switch 
 * the controller to MANUAL mode first (POST /api/mode with {"mode":"manual"}).
 * Only after that request succeeds does it send POST /api/pump with {"state": true/false}.
 */
async function handlePumpToggle() {
  if (state.isRequestPending) return;

  const targetPumpState = !state.deviceData.pump;
  const previousPumpState = state.deviceData.pump;
  const previousMode = state.deviceData.mode;

  state.isRequestPending = true;
  setPumpButtonLoadingState(true);

  try {
    // Step 1: Ensure controller is in MANUAL mode
    if (state.deviceData.mode !== 'manual') {
      showToast('Switching controller to Manual mode...', 'info');

      // Optimistic UI for mode switch
      state.deviceData.mode = 'manual';
      renderModeUI();

      const modeResponse = await apiRequest('/api/mode', {
        method: 'POST',
        body: JSON.stringify({ mode: 'manual' })
      });

      if (!modeResponse || modeResponse.success !== true) {
        throw new Error('Failed to switch controller to Manual mode.');
      }
    }

    // Step 2: Send pump control payload
    // Optimistic UI update for pump state
    state.deviceData.pump = targetPumpState;
    renderPumpUI();

    const pumpResponse = await apiRequest('/api/pump', {
      method: 'POST',
      body: JSON.stringify({ state: targetPumpState })
    });

    if (!pumpResponse || pumpResponse.success !== true) {
      throw new Error(`Device rejected request to turn pump ${targetPumpState ? 'ON' : 'OFF'}.`);
    }

    showToast(`Pump turned ${targetPumpState ? 'ON' : 'OFF'} successfully!`, 'success');
    
    // Refresh status from controller to ensure total state synchronization
    await fetchStatus();
  } catch (error) {
    console.error('[Pump Action Error]', error);

    // Rollback UI to previous confirmed state on failure
    state.deviceData.pump = previousPumpState;
    state.deviceData.mode = previousMode;
    renderPumpUI();
    renderModeUI();

    showToast(`Pump Action Failed: ${error.message}`, 'error');
  } finally {
    state.isRequestPending = false;
    setPumpButtonLoadingState(false);
  }
}

// ==========================================================================
// 8. MODE SWITCH ACTION
// ==========================================================================
async function handleModeSwitch(targetMode) {
  if (state.isRequestPending) return;
  if (state.deviceData.mode === targetMode) return;

  const previousMode = state.deviceData.mode;
  state.isRequestPending = true;

  // Optimistic UI Update
  state.deviceData.mode = targetMode;
  renderModeUI();

  try {
    const data = await apiRequest('/api/mode', {
      method: 'POST',
      body: JSON.stringify({ mode: targetMode })
    });

    if (!data || data.success !== true) {
      throw new Error(`Controller rejected mode change to ${targetMode.toUpperCase()}`);
    }

    showToast(`Switched mode to ${targetMode.toUpperCase()}`, 'success');
    await fetchStatus();
  } catch (error) {
    console.error('[Mode Switch Error]', error);

    // Rollback optimistic state
    state.deviceData.mode = previousMode;
    renderModeUI();

    showToast(`Mode Change Failed: ${error.message}`, 'error');
  } finally {
    state.isRequestPending = false;
  }
}

// ==========================================================================
// 9. CONFIGURATION SAVE & VALIDATION
// ==========================================================================
function validateConfigInputs(startVal, stopVal, maxRuntimeVal) {
  if (isNaN(startVal) || isNaN(stopVal) || isNaN(maxRuntimeVal)) {
    return 'All threshold fields must contain valid numeric values.';
  }
  if (startVal < 0 || startVal > 100) {
    return 'Start threshold must be a percentage between 0% and 100%.';
  }
  if (stopVal < 0 || stopVal > 100) {
    return 'Stop threshold must be a percentage between 0% and 100%.';
  }
  if (startVal > stopVal) {
    return 'Start threshold cannot be greater than Stop threshold.';
  }
  if (maxRuntimeVal <= 0) {
    return 'Maximum runtime must be greater than 0 ms.';
  }
  return null; // Null indicates validation passed
}

async function handleSaveConfig(e) {
  if (e) e.preventDefault();
  if (state.isRequestPending) return;

  const startVal = parseInt(DOM.inputStartThreshold.value, 10);
  const stopVal = parseInt(DOM.inputStopThreshold.value, 10);
  const maxRuntimeVal = parseInt(DOM.inputMaxRuntime.value, 10);

  // Validate values client-side before sending
  const validationError = validateConfigInputs(startVal, stopVal, maxRuntimeVal);
  if (validationError) {
    showConfigFeedback(validationError, true);
    showToast(validationError, 'error');
    return;
  }

  showConfigFeedback('', false);
  state.isRequestPending = true;
  setSaveConfigButtonLoading(true);

  const payload = {
    startThreshold: startVal,
    stopThreshold: stopVal,
    maxRuntimeMs: maxRuntimeVal
  };

  try {
    const response = await apiRequest('/api/config', {
      method: 'POST',
      body: JSON.stringify(payload)
    });

    if (!response || response.success !== true) {
      throw new Error('Device returned failure response when saving configuration.');
    }

    state.configData = { ...payload };
    showConfigFeedback('Configuration saved successfully!', false);
    showToast('Configuration saved to ESP8266!', 'success');
  } catch (error) {
    console.error('[Save Config Error]', error);
    showConfigFeedback(`Save failed: ${error.message}`, true);
    showToast(`Configuration Error: ${error.message}`, 'error');
  } finally {
    state.isRequestPending = false;
    setSaveConfigButtonLoading(false);
  }
}

// ==========================================================================
// 10. UI RENDERING FUNCTIONS
// ==========================================================================
function renderAllUI() {
  renderConnectionBadge();
  renderLastUpdatedText();
  renderSensorErrorBanner();
  renderTelemetryUI();
  renderPumpUI();
  renderModeUI();
  renderDeviceInfoUI();
}

function setConnectionStatus(status) {
  state.connectionStatus = status; // 'connecting' | 'connected' | 'disconnected'
  renderConnectionBadge();
}

function renderConnectionBadge() {
  if (!DOM.connectionBadge || !DOM.connectionText) return;

  DOM.connectionBadge.className = 'connection-badge';

  if (state.connectionStatus === 'connected') {
    DOM.connectionBadge.classList.add('status-connected');
    DOM.connectionText.textContent = 'Connected';
  } else if (state.connectionStatus === 'connecting') {
    DOM.connectionBadge.classList.add('status-connecting');
    DOM.connectionText.textContent = 'Connecting...';
  } else {
    DOM.connectionBadge.classList.add('status-disconnected');
    DOM.connectionText.textContent = 'Disconnected';
  }
}

function renderLastUpdatedText() {
  if (!DOM.lastUpdatedDisplay) return;

  if (!state.lastUpdated) {
    DOM.lastUpdatedDisplay.textContent = 'Never updated';
    return;
  }

  const now = new Date();
  const diffSec = Math.floor((now - state.lastUpdated) / 1000);
  const timeStr = state.lastUpdated.toLocaleTimeString();

  if (state.connectionStatus === 'disconnected') {
    DOM.lastUpdatedDisplay.textContent = `Last update: ${timeStr} • Disconnected`;
  } else if (diffSec < 5) {
    DOM.lastUpdatedDisplay.textContent = `Updated just now (${timeStr})`;
  } else if (diffSec < 60) {
    DOM.lastUpdatedDisplay.textContent = `Updated ${diffSec}s ago`;
  } else {
    DOM.lastUpdatedDisplay.textContent = `Last updated: ${timeStr}`;
  }
}

function renderSensorErrorBanner() {
  if (!DOM.sensorErrorBanner) return;

  if (state.deviceData.sensorError) {
    DOM.sensorErrorBanner.style.display = 'flex';
  } else {
    DOM.sensorErrorBanner.style.display = 'none';
  }
}

function renderTelemetryUI() {
  // Soil Moisture Gauge
  const moisture = state.deviceData.soilMoisture;
  DOM.displayMoistureVal.textContent = state.connectionStatus === 'connected' ? moisture : '--';

  // Update ring gauge circle
  // Perimeter of radius 52 circle = 2 * PI * 52 ≈ 326.7
  const perimeter = 326.7;
  const clampedMoisture = Math.max(0, Math.min(100, moisture));
  const offset = perimeter - (perimeter * (clampedMoisture / 100));
  if (DOM.gaugeRingFill) {
    DOM.gaugeRingFill.style.strokeDashoffset = state.connectionStatus === 'connected' ? offset : perimeter;
  }

  // Moisture Tag
  if (DOM.moistureStatusTag) {
    if (state.connectionStatus !== 'connected') {
      DOM.moistureStatusTag.textContent = 'Status: Disconnected';
    } else if (state.deviceData.sensorError) {
      DOM.moistureStatusTag.textContent = 'Status: Sensor Error';
    } else if (moisture < state.configData.startThreshold) {
      DOM.moistureStatusTag.textContent = 'Status: Soil Dry (Irrigation Needed)';
    } else if (moisture >= state.configData.stopThreshold) {
      DOM.moistureStatusTag.textContent = 'Status: Soil Optimal';
    } else {
      DOM.moistureStatusTag.textContent = 'Status: Normal';
    }
  }

  // Temperature (1 Decimal place display)
  const temp = state.deviceData.temperature;
  DOM.displayTempVal.textContent = state.connectionStatus === 'connected' ? temp.toFixed(1) : '--';

  // Humidity (1 Decimal place display)
  const humidity = state.deviceData.humidity;
  DOM.displayHumidityVal.textContent = state.connectionStatus === 'connected' ? humidity.toFixed(1) : '--';
}

function renderPumpUI() {
  const isPumpOn = state.deviceData.pump;

  // Pump Badge
  if (DOM.pumpStatusBadge && DOM.pumpStatusText) {
    if (isPumpOn) {
      DOM.pumpStatusBadge.className = 'pump-badge pump-on';
      DOM.pumpStatusText.textContent = 'PUMP ON';
    } else {
      DOM.pumpStatusBadge.className = 'pump-badge pump-off';
      DOM.pumpStatusText.textContent = 'PUMP OFF';
    }
  }

  // Main Action Button
  if (DOM.btnTogglePump && DOM.pumpBtnLabel) {
    if (isPumpOn) {
      DOM.btnTogglePump.className = 'btn-pump-main pump-btn-on';
      DOM.pumpBtnLabel.textContent = 'Turn Pump OFF';
    } else {
      DOM.btnTogglePump.className = 'btn-pump-main pump-btn-off';
      DOM.pumpBtnLabel.textContent = 'Turn Pump ON';
    }
  }
}

function renderModeUI() {
  const mode = state.deviceData.mode; // 'auto' | 'manual'

  // Mode Badge
  if (DOM.modeStatusBadge) {
    if (mode === 'auto') {
      DOM.modeStatusBadge.className = 'mode-badge mode-auto';
      DOM.modeStatusBadge.textContent = 'AUTO';
    } else {
      DOM.modeStatusBadge.className = 'mode-badge mode-manual';
      DOM.modeStatusBadge.textContent = 'MANUAL';
    }
  }

  // Segment Buttons
  if (DOM.btnModeAuto && DOM.btnModeManual) {
    if (mode === 'auto') {
      DOM.btnModeAuto.classList.add('active');
      DOM.btnModeManual.classList.remove('active');
    } else {
      DOM.btnModeManual.classList.add('active');
      DOM.btnModeAuto.classList.remove('active');
    }
  }

  // Mode Description Text
  if (DOM.modeDescText) {
    if (mode === 'auto') {
      DOM.modeDescText.innerHTML = '<strong>AUTO Mode:</strong> System automatically activates pump when soil moisture drops below Start Threshold.';
    } else {
      DOM.modeDescText.innerHTML = '<strong>MANUAL Mode:</strong> Automatic threshold triggers disabled. Pump is exclusively controlled manually by user.';
    }
  }
}

function renderConfigFormFields() {
  // Populate form fields only if user is not focused on inputting
  if (document.activeElement !== DOM.inputStartThreshold) {
    DOM.inputStartThreshold.value = state.configData.startThreshold;
  }
  if (document.activeElement !== DOM.inputStopThreshold) {
    DOM.inputStopThreshold.value = state.configData.stopThreshold;
  }
  if (document.activeElement !== DOM.inputMaxRuntime) {
    DOM.inputMaxRuntime.value = state.configData.maxRuntimeMs;
  }
}

function renderDeviceInfoUI() {
  if (DOM.infoDeviceName) DOM.infoDeviceName.textContent = state.deviceData.device;
  if (DOM.infoFirmwareVersion) DOM.infoFirmwareVersion.textContent = `v${state.deviceData.version}`;
  if (DOM.infoIpAddress) DOM.infoIpAddress.textContent = state.deviceData.ip;
  if (DOM.infoUptime) DOM.infoUptime.textContent = formatUptime(state.deviceData.uptime);
  if (DOM.infoWifiStatus) DOM.infoWifiStatus.textContent = state.deviceData.wifi ? 'Connected (Signal OK)' : 'Disconnected';
}

function setPumpButtonLoadingState(isLoading) {
  if (!DOM.btnTogglePump) return;
  DOM.btnTogglePump.disabled = isLoading;

  if (isLoading) {
    DOM.pumpBtnIcon.style.display = 'none';
    DOM.pumpBtnSpinner.style.display = 'block';
  } else {
    DOM.pumpBtnIcon.style.display = 'block';
    DOM.pumpBtnSpinner.style.display = 'none';
  }
}

function setSaveConfigButtonLoading(isLoading) {
  if (!DOM.btnSaveConfig) return;
  DOM.btnSaveConfig.disabled = isLoading;

  if (isLoading) {
    DOM.btnSaveConfigText.style.display = 'none';
    DOM.btnSaveConfigSpinner.style.display = 'inline-block';
  } else {
    DOM.btnSaveConfigText.style.display = 'inline';
    DOM.btnSaveConfigSpinner.style.display = 'none';
  }
}

function showConfigFeedback(message, isError) {
  if (!DOM.configFeedback) return;
  if (!message) {
    DOM.configFeedback.style.display = 'none';
    DOM.configFeedback.textContent = '';
    return;
  }
  DOM.configFeedback.style.display = 'block';
  DOM.configFeedback.className = isError ? 'form-feedback feedback-error' : 'form-feedback feedback-success';
  DOM.configFeedback.textContent = message;
}

// ==========================================================================
// 11. EVENT LISTENERS SETUP
// ==========================================================================
function setupEventListeners() {
  // Manual Connect Button
  if (DOM.btnConnect) {
    DOM.btnConnect.addEventListener('click', async () => {
      const inputVal = DOM.inputEspUrl.value;
      const normalized = saveBaseUrl(inputVal);
      showToast(`Connecting to ${normalized}...`, 'info');
      setConnectionStatus('connecting');

      DOM.btnConnectText.style.display = 'none';
      DOM.btnConnectSpinner.style.display = 'inline-block';
      DOM.btnConnect.disabled = true;

      try {
        const success = await fetchStatus();
        await fetchConfig();
        if (success) {
          showToast(`Successfully connected to ${normalized}`, 'success');
        } else {
          showToast(`Failed to connect to ${normalized}`, 'error');
        }
      } finally {
        DOM.btnConnectText.style.display = 'inline';
        DOM.btnConnectSpinner.style.display = 'none';
        DOM.btnConnect.disabled = false;
      }
    });
  }

  // Connection Preset Pill Buttons
  document.querySelectorAll('.pill-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const presetUrl = btn.getAttribute('data-url');
      saveBaseUrl(presetUrl);
      DOM.btnConnect.click();
    });
  });

  // Pump Toggle Button
  if (DOM.btnTogglePump) {
    DOM.btnTogglePump.addEventListener('click', handlePumpToggle);
  }

  // Mode Segment Toggle Buttons
  if (DOM.btnModeAuto) {
    DOM.btnModeAuto.addEventListener('click', () => handleModeSwitch('auto'));
  }
  if (DOM.btnModeManual) {
    DOM.btnModeManual.addEventListener('click', () => handleModeSwitch('manual'));
  }

  // Save Configuration Form Submit
  if (DOM.configForm) {
    DOM.configForm.addEventListener('submit', handleSaveConfig);
  }
}

// ==========================================================================
// 12. HELPER FUNCTIONS (UPTIME & TOAST)
// ==========================================================================
/**
 * Formats uptime seconds into human readable duration string.
 * Example: 120 -> "2m 0s", 5072 -> "1h 24m 32s", 188100 -> "2d 4h 15m"
 */
function formatUptime(totalSeconds) {
  if (totalSeconds === null || totalSeconds === undefined || isNaN(totalSeconds)) {
    return '--';
  }
  const sec = Math.floor(Math.max(0, totalSeconds));
  const days = Math.floor(sec / 86400);
  const hours = Math.floor((sec % 86400) / 3600);
  const minutes = Math.floor((sec % 3600) / 60);
  const remainingSec = sec % 60;

  if (days > 0) {
    return `${days}d ${hours}h ${minutes}m`;
  }
  if (hours > 0) {
    return `${hours}h ${minutes}m ${remainingSec}s`;
  }
  if (minutes > 0) {
    return `${minutes}m ${remainingSec}s`;
  }
  return `${remainingSec}s`;
}

function showToast(message, type = 'info') {
  if (!DOM.toastContainer) return;

  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;

  let iconSvg = '';
  if (type === 'success') {
    iconSvg = '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="#10b981" stroke-width="2"><polyline points="20 6 9 17 4 12"></polyline></svg>';
  } else if (type === 'error') {
    iconSvg = '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="#ef4444" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="12"></line><line x1="12" y1="16" x2="12.01" y2="16"></line></svg>';
  } else {
    iconSvg = '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="#06b6d4" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="16" x2="12" y2="12"></line><line x1="12" y1="8" x2="12.01" y2="8"></line></svg>';
  }

  toast.innerHTML = `${iconSvg}<span>${escapeHtml(message)}</span>`;
  DOM.toastContainer.appendChild(toast);

  // Auto remove after 3.5 seconds
  setTimeout(() => {
    toast.classList.add('toast-out');
    setTimeout(() => {
      if (toast.parentNode) {
        toast.parentNode.removeChild(toast);
      }
    }, 300);
  }, 3500);
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

// ==========================================================================
// 13. SERVICE WORKER & PWA INSTALL HANDLER
// ==========================================================================
function registerServiceWorker() {
  if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
      navigator.serviceWorker.register('./sw.js')
        .then((reg) => {
          console.log('[ServiceWorker] Registered successfully with scope:', reg.scope);
        })
        .catch((err) => {
          console.warn('[ServiceWorker] Registration failed:', err);
        });
    });
  }
}

function initPWAInstallPrompt() {
  window.addEventListener('beforeinstallprompt', (e) => {
    // Prevent immediate browser mini-infobar
    e.preventDefault();
    state.deferredInstallPrompt = e;

    if (DOM.btnInstallPWA) {
      DOM.btnInstallPWA.style.display = 'flex';
      DOM.btnInstallPWA.addEventListener('click', async () => {
        if (!state.deferredInstallPrompt) return;
        state.deferredInstallPrompt.prompt();
        const { outcome } = await state.deferredInstallPrompt.userChoice;
        console.log(`[PWA] Install prompt outcome: ${outcome}`);
        state.deferredInstallPrompt = null;
        DOM.btnInstallPWA.style.display = 'none';
      });
    }
  });

  window.addEventListener('appinstalled', () => {
    console.log('[PWA] Application successfully installed');
    if (DOM.btnInstallPWA) {
      DOM.btnInstallPWA.style.display = 'none';
    }
  });
}
