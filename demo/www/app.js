const weight = document.getElementById("weight");
const weightLabel = document.getElementById("weightLabel");
const fireOne = document.getElementById("fireOne");
const fireMany = document.getElementById("fireMany");
const count = document.getElementById("count");
const gallery = document.getElementById("gallery");
const statusLine = document.getElementById("status");
const paramsLine = document.getElementById("params");

let inFlight = 0;

function params() {
    const v = Number(weight.value);
    return {
        w: 280 + v * 48,
        h: 210 + v * 36,
        iter: 300 + v * 420,
    };
}

function burstCount() {
    const min = Number(count.min) || 1;
    const max = Number(count.max) || Infinity;
    const n = Math.round(Number(count.value));
    if (!Number.isFinite(n)) return min;
    return Math.min(max, Math.max(min, n));
}

function refreshLabels() {
    const p = params();
    weightLabel.textContent = weight.value;
    paramsLine.textContent = `${p.w}x${p.h}, ${p.iter} iterations, burst ${burstCount()}`;
}

function setStatus() {
    statusLine.textContent = inFlight ? `${inFlight} rendering` : "idle";
}

async function renderOne(slot) {
    const p = params();
    inFlight++;
    setStatus();

    const started = performance.now();
    try {
        const response = await fetch(
            `http://127.0.0.1:8080/threadpool/render?w=${p.w}&h=${p.h}&iter=${p.iter}`,
            {
                cache: "no-store",
            },
        );
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const blob = await response.blob();
        const url = URL.createObjectURL(blob);
        const elapsed = Math.round(performance.now() - started);

        const tile = document.createElement("article");
        tile.className = "tile";
        tile.innerHTML = `<img alt="Mandelbrot render ${slot}"><footer><span>#${slot}</span><span>${elapsed} ms</span></footer>`;
        tile.querySelector("img").src = url;
        gallery.prepend(tile);

        while (gallery.children.length > 24) {
            const old = gallery.lastElementChild;
            const img = old.querySelector("img");
            if (img && img.src) URL.revokeObjectURL(img.src);
            old.remove();
        }
    } catch (err) {
        statusLine.textContent = err.message;
    } finally {
        inFlight--;
        setStatus();
    }
}

let seq = 0;

fireOne.addEventListener("click", () => {
    renderOne(++seq);
});

fireMany.addEventListener("click", () => {
    const burst = burstCount();
    for (let i = 0; i < burst; i++) renderOne(++seq);
});

weight.addEventListener("input", refreshLabels);
count.addEventListener("input", refreshLabels);
refreshLabels();
