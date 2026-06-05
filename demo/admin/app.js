const STORAGE_KEY = "threadpool-admin-settings-v3";

// ─────────────────────────────────────────────────────────────────────────────
//  EDIT HERE to point the page at a different backend.
//
//  These are the DEFAULTS used on first load (and whenever you clear saved
//  settings). They are also editable live in the UI; saved values in
//  localStorage win over these until you hit "Reset to defaults" / clear storage.
//
//  Two ways the page reaches the admin backend:
//    1. Behind the run_demo.sh proxy (URL path starts with /threadpool/) — the
//       page auto-derives the admin origin and only ADMIN_PORT matters.
//    2. Standalone (opening app.js's index.html directly, no proxy) — the
//       ADMIN_DIRECT fallback below is used verbatim.
// ─────────────────────────────────────────────────────────────────────────────
const CONFIG = {
    // Public admin port. Behind the proxy the admin API lives on its OWN origin
    // (separate port) so its polls use a separate browser connection pool and
    // never queue behind heavy work. MUST match ADMIN_PORT_PUB in
    // run_demo.sh.
    ADMIN_PORT: 9099,

    // Path prefix that signals "we're behind the run_demo.sh proxy". When the
    // page URL starts with this, the admin URL is auto-derived.
    PROXY_PREFIX: "/threadpool/",

    // Path appended to the admin origin behind the proxy, e.g. host:9099/admin.
    ADMIN_PATH: "/admin",

    // Standalone fallback (no proxy): full URL hits the admin listener directly.
    ADMIN_DIRECT: "http://127.0.0.1:9090/admin",

    // Default stats/logs poll interval (ms).
    POLL_RATE: "1000",
};

const els = {
    adminBase: document.getElementById("adminBase"),
    pollRate: document.getElementById("pollRate"),
    saveSettings: document.getElementById("saveSettings"),
    healthDot: document.getElementById("healthDot"),
    healthText: document.getElementById("healthText"),
    lastSeen: document.getElementById("lastSeen"),
    workers: document.getElementById("workers"),
    modeLabel: document.getElementById("modeLabel"),
    busy: document.getElementById("busy"),
    busyDetail: document.getElementById("busyDetail"),
    queueDepth: document.getElementById("queueDepth"),
    activeConnections: document.getElementById("activeConnections"),
    totalServed: document.getElementById("totalServed"),
    pressureSummary: document.getElementById("pressureSummary"),
    busyBar: document.getElementById("busyBar"),
    busyPercent: document.getElementById("busyPercent"),
    queueBar: document.getElementById("queueBar"),
    queuePercent: document.getElementById("queuePercent"),
    pauseLogs: document.getElementById("pauseLogs"),
    clearLogs: document.getElementById("clearLogs"),
    logs: document.getElementById("logs"),
    logMeta: document.getElementById("logMeta"),
};

let settings = loadSettings();
let timer = null;
let logsPaused = false;

// True when the page is served behind the run_demo.sh proxy (so we can derive
// same-origin URLs instead of using the hardcoded *_DIRECT fallbacks).
function behindProxy() {
    return (
        (location.protocol === "http:" || location.protocol === "https:") &&
        location.pathname.startsWith(CONFIG.PROXY_PREFIX)
    );
}

function adminOriginDefault(fallback) {
    return behindProxy()
        ? `${location.protocol}//${location.hostname}:${CONFIG.ADMIN_PORT}${CONFIG.ADMIN_PATH}`
        : fallback;
}

function defaultSettings() {
    return {
        adminBase: adminOriginDefault(CONFIG.ADMIN_DIRECT),
        pollRate: CONFIG.POLL_RATE,
    };
}

function loadSettings() {
    try {
        return {
            ...defaultSettings(),
            ...JSON.parse(localStorage.getItem(STORAGE_KEY) || "{}"),
        };
    } catch {
        return defaultSettings();
    }
}

function saveSettings() {
    settings = {
        adminBase: stripTrailingSlash(els.adminBase.value.trim()),
        pollRate: els.pollRate.value,
    };
    localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
    schedule();
    refreshNow();
    connectEvents(); /* adminBase may have changed -> reconnect the stream */
}

function stripTrailingSlash(value) {
    return value.replace(/\/+$/, "");
}

function hydrateSettings() {
    els.adminBase.value = settings.adminBase;
    els.pollRate.value = settings.pollRate;
}

function adminUrl(path) {
    return `${settings.adminBase}${path}`;
}

async function fetchText(url) {
    const response = await fetch(url, { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.text();
}

function setHealth(state, text) {
    els.healthDot.className = `dot dot-${state}`;
    els.healthText.textContent = text;
}

function numberText(value) {
    return Number.isFinite(value) ? value.toLocaleString() : "--";
}

function setBar(el, percent) {
    el.classList.toggle("hot", percent >= 75 && percent < 100);
    el.classList.toggle("critical", percent >= 100);
    el.style.width = `${Math.max(0, Math.min(100, percent))}%`;
}

function renderStats(stats) {
    const workers = Number(stats.workers);
    const busy = Number(stats.busy);
    const queue = Number(stats.queue_depth);
    const active = Number(stats.active_connections);
    const total = Number(stats.total_served);
    const utilization = workers > 0 ? Math.round((busy / workers) * 100) : 0;
    const queueScale = workers > 0 ? workers * 2 : Math.max(1, queue);
    const queuePressure = Math.round((queue / queueScale) * 100);

    els.workers.textContent = numberText(workers);
    els.modeLabel.textContent = workers === 0 ? "naive mode" : "pool mode";
    els.busy.textContent = numberText(busy);
    els.busyDetail.textContent =
        workers > 0 ? `${utilization}% of workers busy` : "detached threads";
    els.queueDepth.textContent = numberText(queue);
    els.activeConnections.textContent = numberText(active);
    els.totalServed.textContent = numberText(total);
    els.busyPercent.textContent = workers > 0 ? `${utilization}%` : "n/a";
    els.queuePercent.textContent = numberText(queue);

    setBar(els.busyBar, utilization);
    setBar(els.queueBar, queuePressure);

    if (workers === 0) {
        els.pressureSummary.textContent =
            "Naive mode is active; the pool metrics are intentionally zeroed.";
    } else if (queue > 0 && busy >= workers) {
        els.pressureSummary.textContent =
            "The pool is saturated and new render work is queued.";
    } else if (busy > 0) {
        els.pressureSummary.textContent =
            "Render work is running, with worker capacity still available.";
    } else {
        els.pressureSummary.textContent =
            "Workers are idle and the render queue is empty.";
    }
}

function renderLogs(text) {
    if (logsPaused) return;

    const trimmed = text.trimEnd();
    els.logs.textContent = trimmed || "No request logs yet.";
    const lineCount = trimmed ? trimmed.split("\n").length : 0;
    els.logMeta.textContent = `${lineCount} log line${lineCount === 1 ? "" : "s"} shown.`;
}

let events = null;

// Stats arrive in REAL TIME over Server-Sent Events: the server pushes a snapshot
// the instant a gauge changes, so short bursts are never lost in a polling gap.
// EventSource auto-reconnects on error.
function connectEvents() {
    if (events) events.close();
    try {
        events = new EventSource(adminUrl("/events"));
    } catch {
        return;
    }
    events.onopen = () => setHealth("ok", "Live (stream)");
    events.onmessage = (event) => {
        try {
            renderStats(JSON.parse(event.data));
            setHealth("ok", "Live (stream)");
            els.lastSeen.textContent = new Date().toLocaleTimeString();
        } catch {
            /* ignore malformed frame */
        }
    };
    events.onerror = () => setHealth("error", "Reconnecting…");
}

// Logs are still polled (they don't need sub-second fidelity); stats do not poll.
async function refreshNow() {
    try {
        renderLogs(await fetchText(adminUrl("/logs")));
    } catch {
        /* logs unavailable; SSE drives the health indicator */
    }
}

function schedule() {
    clearInterval(timer);
    timer = setInterval(refreshNow, Number(settings.pollRate));
}

els.saveSettings.addEventListener("click", saveSettings);
els.pollRate.addEventListener("change", saveSettings);
els.pauseLogs.addEventListener("click", () => {
    logsPaused = !logsPaused;
    els.pauseLogs.textContent = logsPaused ? "Resume" : "Pause";
});
els.clearLogs.addEventListener("click", () => {
    els.logs.textContent = "Log view cleared. Polling will refill it.";
    els.logMeta.textContent = "View cleared locally.";
});
hydrateSettings();
schedule();
refreshNow();
connectEvents();
