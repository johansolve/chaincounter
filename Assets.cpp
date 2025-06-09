#include "Assets.h"

// HTML for client
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
	<head>
		<meta charset="UTF-8" />
		<meta name="viewport" content="width=device-width, initial-scale=1.0" />
		<title data-i18n="title">Chain Counter</title>
		<link rel="stylesheet" href="/style.css" />
	</head>
	<body class="disconnected">
		<div class="viewport">
		<header>
		  <h1>
			<svg xmlns="http://www.w3.org/2000/svg" class="h-6 w-6" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
			  <path stroke-linecap="round" stroke-linejoin="round" d="M13.828 10.172a4 4 0 00-5.656 0l-4 4a4 4 0 105.656 5.656l1.102-1.101m-.758-4.899a4 4 0 005.656 0l4-4a4 4 0 00-5.656-5.656l-1.1 1.1" />
			</svg>
			<span data-i18n="chainStatus">Chain Status</span>
		  </h1>
		  <button id="settingsBtn" class="icon-button" data-i18n-title="settings" title="Settings">
			<svg xmlns="http://www.w3.org/2000/svg" class="h-6 w-6" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
			  <path stroke-linecap="round" stroke-linejoin="round" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z" />
			  <path stroke-linecap="round" stroke-linejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
			</svg>
		  </button>
		</header>
		<main>
		  <div>
			<div class="large-text chain-length">
			  <span id="rodeLength">--</span> <span>m</span>
			</div>
			<div class="medium-text chain-speed">
			  <svg id="chain-direction" xmlns="http://www.w3.org/2000/svg" class="h-6 w-6" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
				<path stroke-linecap="round" stroke-linejoin="round" d="M19 14l-7 7m0 0l-7-7m7 7V3" />
			  </svg>
			  <span id="speedValue">--</span> <span>m/s</span>
			</div>
		  </div>
		</main>
	</div>
	<details>
		<summary></summary>
		<div class="log"></div>
	</details>
	<div id="settingsPanel" class="slide-over">
	  <button id="closePanel" class="close-btn" data-i18n-title="close" title="Close">
		<svg xmlns="http://www.w3.org/2000/svg" class="h-6 w-6" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
			<path stroke-linecap="round" stroke-linejoin="round" d="M6 18L18 6M6 6l12 12" />
		</svg>
	</button>
	<h2 data-i18n="settings">Settings</h2>
	<div class="form-group">
		<label for="linksPerRev" data-i18n="linksPerPulse">Links per pulse</label>
		<input type="number" id="linksPerRev" />
	</div>
	<div class="form-group">
		<label for="linksPerMeter" data-i18n="linksPerMeter">Links per meter</label>
		<input type="number" id="linksPerMeter" />
	</div>
	<div class="checkbox">
		<input type="checkbox" id="mqttEnable" />
		<label for="mqttEnable" data-i18n="mqttEnable">Send data to SignalK via MQTT</label>
	</div>
	<div id="mqttSettings" class="hidden">
		<div class="form-group">
			<label for="mqttHost" data-i18n="mqttHost">MQTT host</label>
			<input type="text" id="mqttHost" data-i18n-placeholder="mqttHostPlaceholder" placeholder="IP or hostname" />
		</div>
		<div class="form-group">
			<label for="mqttPort" data-i18n="mqttPort">MQTT port</label>
			<input type="number" id="mqttPort" value="1883" />
		</div>
		<div class="form-group">
			<label for="mqttTopicRodeLength" data-i18n="mqttTopicLength">MQTT topic for length</label>
			<input type="text" id="mqttTopicRodeLength" placeholder="vessels/self/navigation/anchor/rodeLength">
		</div>
		<div class="form-group">
			<label for="mqttTopicRodeVelocity" data-i18n="mqttTopicVelocity">MQTT topic for velocity</label>
			<input type="text" id="mqttTopicRodeVelocity" placeholder="vessels/self/navigation/anchor/rodeVelocity">
		</div>
		</div>
		<div class="checkbox">
			<input type="checkbox" id="resetRodeLength" />
			<label for="resetRodeLength" data-i18n="resetChain">Reset chain length to zero</label>
		</div>
		<button id="saveSettings" class="button save-button" data-i18n="save">Save</button>
	</div>


	<script>
		document.addEventListener('DOMContentLoaded', initApp);

		// global object for latest settings
		let latestSettings = {};

		// Internationalization
		const i18n = {
			sv: {
				title: "Kättingräknare",
				chainStatus: "Kättingstatus",
				settings: "Inställningar",
				close: "Stäng",
				linksPerPulse: "Länkar per varv",
				linksPerMeter: "Länkar per meter",
				mqttEnable: "Skicka data till SignalK via MQTT",
				mqttHost: "MQTT-värd",
				mqttPort: "MQTT-port",
				mqttTopicLength: "MQTT-topic för längd",
				mqttTopicVelocity: "MQTT-topic för hastighet",
				mqttHostPlaceholder: "IP eller hostname",
				resetChain: "Nollställ meter kätting",
				save: "Spara",
				invalidJson: "Ogiltigt JSON:",
				localTime: "Lokal tid i format HH:MM:SS",
				writeToConsole: "Skriv till konsolen",
				writeToLog: "Skriv till loggpanelen",
				scrollToBottom: "Scrolla till botten"
			},
			en: {
				title: "Chain Counter",
				chainStatus: "Chain Status",
				settings: "Settings",
				close: "Close",
				linksPerPulse: "Links per pulse",
				linksPerMeter: "Links per meter",
				mqttEnable: "Send data to SignalK via MQTT",
				mqttHost: "MQTT host",
				mqttPort: "MQTT port",
				mqttTopicLength: "MQTT topic for length",
				mqttTopicVelocity: "MQTT topic for velocity",
				mqttHostPlaceholder: "IP or hostname",
				resetChain: "Reset chain length to zero",
				save: "Save",
				invalidJson: "Invalid JSON:",
				localTime: "Local time in HH:MM:SS format",
				writeToConsole: "Write to console",
				writeToLog: "Write to log panel",
				scrollToBottom: "Scroll to bottom"
			}
		};

		function detectBrowserLanguage() {
			// Get browser languages in order of preference
			const browserLangs = navigator.languages || [navigator.language || navigator.userLanguage || 'en'];
			
			// Available languages in our app
			const availableLangs = Object.keys(i18n);
			
			// Find the best match
			for (const browserLang of browserLangs) {
				// Try exact match first
				if (availableLangs.includes(browserLang)) {
					return browserLang;
				}
				
				// Try language code without region (e.g., 'sv' from 'sv-SE')
				const langCode = browserLang.split('-')[0];
				if (availableLangs.includes(langCode)) {
					return langCode;
				}
			}
			
			// Default to English if no match found
			return 'en';
		}

		function updateLanguage(lang) {
			const texts = i18n[lang] || i18n.en;

			// Update document title
			document.title = texts.title;

			// Update HTML lang attribute
			document.documentElement.setAttribute('lang', lang);

			// Update all elements with data-i18n attribute
			document.querySelectorAll('[data-i18n]').forEach(el => {
				const key = el.getAttribute('data-i18n');
				if (texts[key]) {
					el.textContent = texts[key];
				}
			});

			// Update title attributes
			document.querySelectorAll('[data-i18n-title]').forEach(el => {
				const key = el.getAttribute('data-i18n-title');
				if (texts[key]) {
					el.setAttribute('title', texts[key]);
				}
			});

			// Update placeholder attributes
			document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
				const key = el.getAttribute('data-i18n-placeholder');
				if (texts[key]) {
					el.setAttribute('placeholder', texts[key]);
				}
			});
		}

		function initApp() {
			cacheElements();
			bindUIEvents();
			
			// Auto-detect and set language based on browser preferences
			const detectedLang = detectBrowserLanguage();
			updateLanguage(detectedLang);
			
			setupWebSocket();
			clearDisplay();
		}

		function clearDisplay() {
			el.rodeLength.textContent = '--';
			el.speedValue.textContent = '--';
			el.chainIcon.classList.remove('up');
			el.chainIcon.classList.remove('down');
			document.body.classList.add('disconnected');
		}

		// cache all DOM elements we need
		const el = {};
		function cacheElements() {
			el.rodeLength   = document.getElementById('rodeLength');
			el.speedValue   = document.getElementById('speedValue');
			el.chainIcon    = document.getElementById('chain-direction');
			el.settingsBtn  = document.getElementById('settingsBtn');
			el.closePanel   = document.getElementById('closePanel');
			el.panel        = document.getElementById('settingsPanel');
			el.linksPerRev  = document.getElementById('linksPerRev');
			el.linksPerMeter= document.getElementById('linksPerMeter');
			el.resetRode    = document.getElementById('resetRodeLength');
			el.mqttEnable   = document.getElementById('mqttEnable');
			el.mqttSettings = document.getElementById('mqttSettings');
			el.mqttHost     = document.getElementById('mqttHost');
			el.mqttPort     = document.getElementById('mqttPort');
			el.mqttTopicLen = document.getElementById('mqttTopicRodeLength');
			el.mqttTopicVel = document.getElementById('mqttTopicRodeVelocity');
			el.saveBtn      = document.getElementById('saveSettings');
		}

		// connect event listeners
		function bindUIEvents() {
			el.settingsBtn.addEventListener('click', openSettings);
			el.closePanel.addEventListener('click', () => {
				el.panel.classList.remove('open');
			});
			el.mqttEnable.addEventListener('change', () => {
				el.mqttSettings.classList.toggle('hidden', !el.mqttEnable.checked);
			});
			el.saveBtn.addEventListener('click', saveSettings);
		}

		function openSettings() {
			el.linksPerRev.value       = latestSettings.linksPerRev     ?? '';
			el.linksPerMeter.value     = latestSettings.linksPerMeter   ?? '';
			el.resetRode.checked       = false;
			el.mqttEnable.checked      = !!latestSettings.mqttEnable;
			el.mqttHost.value          = latestSettings.mqttHost        || '';
			el.mqttPort.value          = latestSettings.mqttPort        || 1883;
			el.mqttTopicLen.value      = latestSettings.mqttTopicRodeLength || '';
			el.mqttTopicVel.value      = latestSettings.mqttTopicRodeVelocity || '';
			el.mqttSettings.classList.toggle('hidden', !el.mqttEnable.checked);
			el.panel.classList.add('open');
		}

		function saveSettings() {
			const cfg = {
				linksPerRev:          parseFloat(el.linksPerRev.value)     || 0,
				linksPerMeter:        parseFloat(el.linksPerMeter.value)   || 0,
				reset:                el.resetRode.checked ? 1 : 0,
				mqttEnable:           el.mqttEnable.checked ? 1 : 0,
				mqttHost:             el.mqttHost.value.trim(),
				mqttPort:             parseInt(el.mqttPort.value, 10)      || 1883,
				mqttTopicRodeLength:  el.mqttTopicLen.value.trim(),
				mqttTopicRodeVelocity:el.mqttTopicVel.value.trim()
			};
			latestSettings = { ...latestSettings, ...cfg };
			ws.send(JSON.stringify(cfg));
			el.panel.classList.remove('open');
			el.resetRode.checked = false;
		}

		// WebSocket
		let ws;

		const HEARTBEAT_INTERVAL   = 15000;
		const HEARTBEAT_TIMEOUT_MS = 5000;

		let hbTimer, pongTimer;

		function setupWebSocket() {
			clearDisplay();

			const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
			ws = new WebSocket(`${protocol}//${location.host}/ws`);

			ws.onopen = () => {
				logger('✅ WS open');
				startHeartbeat();
			};
			ws.onerror = e => {
				console.error('❌ WS error', e);
				clearDisplay();
				stopHeartbeat();
			};
			ws.onclose = () => {
				logger('ℹ️ WS closed, reconnect in 5s');
				clearDisplay();
				stopHeartbeat();
				setTimeout(setupWebSocket, 5000);
			};
			ws.onmessage = evt => {
				let data;
				try {
					data = JSON.parse(evt.data);
				} catch (err) {
					console.warn('Invalid JSON:', evt.data);
					return;
				}
				if (data.pong) {
					// got response on ping
					clearTimeout(pongTimer);
					return;
				}
				handleMessage(data);
			};
		}

		function startHeartbeat() {
			// send ping every HEARTBEAT_INTERVAL
			hbTimer = setInterval(() => {
				if (ws.readyState !== WebSocket.OPEN) return;
				ws.send(JSON.stringify({ ping: Date.now() }));
				// if no pong is received before timeout, force close
				pongTimer = setTimeout(() => {
					console.warn('💤 ws timeout – reconnect');
					ws.close();
					clearDisplay();
				}, HEARTBEAT_TIMEOUT_MS);
			}, HEARTBEAT_INTERVAL);
		}

		function stopHeartbeat() {
			clearInterval(hbTimer);
			clearTimeout(pongTimer);
		}

		function handleMessage(data) {
			if (data.version) {
				logger('chaincounter version ' + data.version);
			}
			if (data.debug) {
				logger(data.debug);
			}
			if (typeof data.length === 'undefined') {
				return;
			}
			document.body.classList.remove('disconnected');
			updateDisplay({
				length:    data.length,
				velocity:  data.velocity,
				direction: data.direction,
				moving:    data.moving
			});
			Object.assign(latestSettings, pick(data,
				'linksPerRev','linksPerMeter','mqttEnable','mqttHost','mqttPort','mqttTopicRodeLength','mqttTopicRodeVelocity'
			));
			if (!isSettingsComplete(latestSettings)) {
				el.panel.classList.add('open');
			}
		}

		function pick(obj, ...keys) {
			return keys.reduce((o, k) => {
				if (obj[k] !== undefined) {
					o[k] = obj[k];
				}
				return o;
			}, {});
		}

		function isSettingsComplete(s) {
			const req = ['linksPerRev','linksPerMeter'];
			if (req.some(k => !s[k])) {
				return false;
			}
			if (s.mqttEnable) {
				const m = ['mqttHost','mqttPort','mqttTopicRodeLength','mqttTopicRodeVelocity'];
				if (m.some(k => !s[k])) {
					return false;
				}
			}
			return true;
		}

		function updateDisplay({ length = 0, velocity = 0, direction = 0, moving = false }) {
			el.rodeLength.textContent = length.toFixed(1);
			el.speedValue.textContent = velocity.toFixed(1);
			el.chainIcon.classList.toggle('down', direction === 1 && moving);
			el.chainIcon.classList.toggle('up',   direction === 0 && moving);
		}

		function logger(...args) {
			const logElement = document.querySelector('details .log');
			if (!logElement) return console.log(...args); // fallback

			// Local time in format HH:MM:SS
			const now = new Date();
			const pad = n => n.toString().padStart(2, '0');
			const time = `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;

			const text = args.map(a => (typeof a === 'object' ? JSON.stringify(a) : a)).join(' ');
			const line = `[${time}] ${text}`;

			// Write to console
			console.log(line);

			// Write to log panel
			const div = document.createElement('div');
			div.textContent = line;
			logElement.appendChild(div);

			// Scroll to bottom
			logElement.scrollTop = logElement.scrollHeight;
		}
	</script>

  </body>
  </html>
)rawliteral";

// CSS for client
const char style_css[] PROGMEM = R"rawliteral(
*, *::before, *::after {
	box-sizing: border-box;
}

body {
	margin: 0;
	font-family: sans-serif;
	background-color: #1a202c;
	color: white;

	&.disconnected header {
		opacity: 0.5;
		h1 svg {
			color: transparent;
		}
		.icon-button {
			visibility: hidden;
		}
	}

	&:not(.disconnected) .slide-over.open {
		transform: translateX(0);
	}
}

.viewport {
	display: flex;
	flex-direction: column;
	height: calc(100vh - 1rem);
	height: calc(100dvh - 1rem);
}

.hidden {
	display: none;
}

header {
	display: flex;
	justify-content: space-between;
	align-items: center;
	padding: 1rem;
	background-color: #2d3748;

	.simulate {
		text-align: center;
	}

	button.control {
		min-width: auto;
		width: 3rem;
		height: 3rem;
		padding: 0;
		background-color: #555;
		border-radius: 100rem;
		box-shadow: 0 1px 5px #000, 0 0 5px inset #777;

		&:active {
			box-shadow: 0 0 3px #000, 0 0 5px inset #333;
		}
	}
}

/* Headings */
h1, h2 {
	display: flex;
	align-items: center;
	gap: 0.5rem;
}
h1 {
	font-size: 1.5rem;
}
h2 {
	font-size: 1.25rem;
	margin-bottom: 1rem;
}

main {
	display: flex;
	flex: 1;
	flex-direction: column;
	justify-content: center;
	align-items: center;
	text-align: center;
	gap: 1rem;
}

/* Text sizes with shared layout */
.large-text, .medium-text {
	display: flex;
	align-items: center;
	justify-content: center;
	gap: 0.75rem;
}
.large-text {
	font-size: clamp(6rem, 30vw, 12rem);
	font-weight: bold;
}
.medium-text {
	font-size: clamp(1.2rem, 5vw, 2rem);
}

.symbol {
	svg {
		width: 3rem;
		height: 3rem;
	}
}

.chain-speed {
	svg {
		--svg-size: clamp(4rem, 10vw, 6rem);
		width: var(--svg-size);
		height: var(--svg-size);
		visibility: hidden;
		border-radius: 100rem;
		color: black;

		&.up {
			visibility: visible;
			background-color: red;
			transform: rotate(180deg);
		}
		&.down {
			visibility: visible;
			background-color: green;
		}
	}
}

details {
	> summary {
		font-size: 1rem;
		line-height: 1;
		height: 1rem;
	}
}

.log {
	background-color: #fff;
	font-family: monospace;
	font-size: 0.8rem;
	color: #000;
	padding: 0.5rem;
	height: 16rem;
	overflow-y: auto;

	div {
		padding: 0.2rem 0;
		border-bottom: 1px dotted silver;
	}
}

.slide-over {
	position: fixed;
	top: 0;
	right: 0;
	width: 320px;
	height: 100%;
	background-color: #2d3748;
	padding: 1.5rem;
	transform: translateX(100%);
	transition: transform 0.3s ease-in-out;
	overflow-y: auto;
	z-index: 1000;
}

/* Form controls */
.form-group {
	margin-bottom: 1rem;

	label {
		display: block;
		font-size: 0.9rem;
		margin-bottom: 0.25rem;
	}

	input {
		width: 100%;
		padding: 0.5rem;
		background-color: #4a5568;
		border: none;
		color: white;
		border-radius: 4px;
	}
}

.checkbox {
	display: flex;
	align-items: center;
	margin-bottom: 1rem;

	input {
		margin-right: 0.5rem;
	}
}

/* Button styles */
.button, .close-btn, .icon-button {
	border: none;
	cursor: pointer;
	color: white;
	background: none;
}

.button {
	width: 100%;
	padding: 0.75rem;
	background-color: #3182ce;
	border-radius: 4px;

	&.save-button {
		margin-top: 1rem;
	}
}

.close-btn {
	position: absolute;
	top: 1rem;
	right: 1rem;
	font-size: 1.25rem;
}

.icon-button {
	font-size: 1.5rem;
}

svg {
	width: 2rem;
	height: 2rem;
}
)rawliteral";