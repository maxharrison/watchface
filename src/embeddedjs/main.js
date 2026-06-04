import Poco from "commodetto/Poco";
import Battery from "embedded:sensor/Battery";
import Location from "embedded:sensor/Location";

const render = new Poco(screen);

// ----------------------------------------------------------------------------
// CONFIG
// ----------------------------------------------------------------------------
// Alloy has no native calendar API. To show your next event, point this at an
// endpoint (your own tiny server, or a service) that returns JSON shaped like:
//   { "start": "2026-06-03T14:30:00", "title": "Standup" }
// Leave it as "" to disable the calendar row (the watchface still runs fine).
const CALENDAR_URL = "";

// ----------------------------------------------------------------------------
// FONTS  (built-in Pebble fonts — swap for custom Jersey font later if you like)
// ----------------------------------------------------------------------------
const timeFont  = new render.Font("Bitham-Bold", 42);  // primary: HH:MM
const dateFont  = new render.Font("Gothic-Bold", 18);  // date
const smallFont = new render.Font("Gothic-Regular", 18); // info rows

// ----------------------------------------------------------------------------
// COLORS  (minimal & clean: mostly mono, one accent)
// ----------------------------------------------------------------------------
const black  = render.makeColor(0, 0, 0);
const white  = render.makeColor(255, 255, 255);
const dim    = render.makeColor(150, 150, 150);   // secondary text
const accent = render.makeColor(0, 200, 170);     // single accent color
const green  = render.makeColor(0, 170, 0);
const yellow = render.makeColor(255, 170, 0);
const red    = render.makeColor(255, 0, 0);

// ----------------------------------------------------------------------------
// LABELS
// ----------------------------------------------------------------------------
const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

// Round (Gabbro) clips the corners, so keep content away from the edges.
const isRound = render.width === render.height;

// ----------------------------------------------------------------------------
// STATE  (updated by sensor / connection / network events)
// ----------------------------------------------------------------------------
let lastDate      = new Date();
let batteryPercent = 100;
let isConnected   = true;
let weather       = null;   // { temp, conditions }
let heartRate     = null;   // number, or null if unavailable
let nextEvent     = null;   // { time: "HH:MM", title }

// ----------------------------------------------------------------------------
// SENSORS
// ----------------------------------------------------------------------------

// Battery — continuous monitor.
const battery = new Battery({
    onSample() {
        batteryPercent = this.sample().percent;
        drawScreen();
    }
});
batteryPercent = battery.sample().percent;

// Heart rate — best-effort. The exact Alloy module name is unconfirmed in the
// current Developer Preview, so we guard it: if it isn't available the row
// simply shows "--" and nothing breaks. Verify against the Sensors guide.
(async () => {
    try {
        const { default: HeartRate } = await import("embedded:sensor/HeartRate");
        const hr = new HeartRate({
            onSample() {
                const s = this.sample();
                // Field name may be `rate` or `heartRate` depending on the build.
                heartRate = (s.rate ?? s.heartRate ?? null);
                drawScreen();
            }
        });
        const s = hr.sample?.();
        if (s) heartRate = (s.rate ?? s.heartRate ?? null);
    } catch (e) {
        // Module not present in this SDK build — leave heartRate null.
        trace("HeartRate sensor unavailable: " + e + "\n");
    }
})();

// Connection status to the phone app.
function checkConnection() {
    isConnected = watch.connected.app;
    drawScreen();
}
watch.addEventListener("connected", checkConnection);
checkConnection();

// ----------------------------------------------------------------------------
// NETWORK  (weather + calendar, refreshed hourly via the hourchange event)
// ----------------------------------------------------------------------------

let location = null;

function requestLocation() {
    location = new Location({
        onSample() {
            const s = this.sample();
            this.close(); // Location is a one-shot request, not a monitor.
            fetchWeather(s.latitude, s.longitude);
        }
    });
}

function weatherDescription(code) {
    if (code === 0)  return "Clear";
    if (code <= 3)   return "Cloudy";
    if (code <= 48)  return "Fog";
    if (code <= 57)  return "Drizzle";
    if (code <= 67)  return "Rain";
    if (code <= 77)  return "Snow";
    if (code <= 82)  return "Showers";
    if (code <= 86)  return "Snow";
    if (code <= 99)  return "Storm";
    return "--";
}

async function fetchWeather(latitude, longitude) {
    try {
        const url = new URL("http://api.open-meteo.com/v1/forecast");
        url.search = new URLSearchParams({
            latitude,
            longitude,
            current: "temperature_2m,weather_code"
        });
        const res = await fetch(url);
        const data = await res.json();
        weather = {
            temp: Math.round(data.current.temperature_2m),
            conditions: weatherDescription(data.current.weather_code)
        };
        drawScreen();
    } catch (e) {
        trace("Weather fetch error: " + e + "\n");
    }
}

async function fetchNextEvent() {
    if (!CALENDAR_URL) return;            // disabled — clean no-op
    try {
        const res = await fetch(CALENDAR_URL);
        const data = await res.json();    // expects { start, title }
        if (data && data.start) {
            const d = new Date(data.start);
            const hh = String(d.getHours()).padStart(2, "0");
            const mm = String(d.getMinutes()).padStart(2, "0");
            // Trim the title so it fits the small screen.
            const title = String(data.title ?? "").slice(0, 14);
            nextEvent = { time: `${hh}:${mm}`, title };
        } else {
            nextEvent = null;
        }
        drawScreen();
    } catch (e) {
        trace("Calendar fetch error: " + e + "\n");
    }
}

function refreshNetwork() {
    requestLocation();   // → weather
    fetchNextEvent();    // → calendar
}
watch.addEventListener("hourchange", refreshNetwork); // fires immediately too

// ----------------------------------------------------------------------------
// DRAWING HELPERS
// ----------------------------------------------------------------------------

function centerText(str, font, color, y) {
    const w = render.getTextWidth(str, font);
    render.drawText(str, font, color, (render.width - w) / 2, y);
}

function drawBatteryBar() {
    const barWidth  = (render.width / 2) | 0;
    const barX      = ((render.width - barWidth) / 2) | 0;
    const barY      = isRound ? 18 : 12;
    const barHeight = 8;

    // Hollow white border.
    render.fillRectangle(white, barX, barY, barWidth, barHeight);
    render.fillRectangle(black, barX + 1, barY + 1, barWidth - 2, barHeight - 2);

    let barColor = green;
    if (batteryPercent <= 20)      barColor = red;
    else if (batteryPercent <= 40) barColor = yellow;

    const fillWidth = ((batteryPercent * (barWidth - 4)) / 100) | 0;
    render.fillRectangle(barColor, barX + 2, barY + 2, fillWidth, barHeight - 4);

    // Bluetooth disconnect marker: small red "x" to the right of the bar.
    if (!isConnected) {
        render.drawText("x", smallFont, red, barX + barWidth + 6, barY - 5);
    }
}

// ----------------------------------------------------------------------------
// MAIN DRAW
// ----------------------------------------------------------------------------

function drawScreen(event) {
    // Non-time redraws (battery/HR/network) arrive with no date — fall back.
    const now = event?.date ?? lastDate;
    if (event?.date) lastDate = event.date;

    render.begin();
    render.fillRectangle(black, 0, 0, render.width, render.height);

    drawBatteryBar();

    // --- Build the centered content block ---
    const gap = 6;
    const rowH = smallFont.height;
    const blockH = timeFont.height + dateFont.height + rowH + rowH + gap * 3;
    let y = ((render.height - blockH) / 2) | 0;
    if (y < 36) y = 36; // keep clear of the battery bar on shorter screens

    // Time HH:MM
    const hours = String(now.getHours()).padStart(2, "0");
    const minutes = String(now.getMinutes()).padStart(2, "0");
    centerText(`${hours}:${minutes}`, timeFont, white, y);
    y += timeFont.height + gap;

    // Date "Mon Jun 03"
    const dateStr = `${DAYS[now.getDay()]} ${MONTHS[now.getMonth()]} ${String(now.getDate()).padStart(2, "0")}`;
    centerText(dateStr, dateFont, dim, y);
    y += dateFont.height + gap;

    // Info row: heart rate + weather
    const hrStr = "HR " + (heartRate != null ? heartRate : "--");
    const wxStr = weather ? `${weather.temp}\u00B0 ${weather.conditions}` : "...";
    centerText(`${hrStr}   ${wxStr}`, smallFont, accent, y);
    y += rowH + gap;

    // Next event row
    const evStr = nextEvent ? `${nextEvent.time} ${nextEvent.title}` : "No event";
    centerText(evStr, smallFont, dim, y);

    render.end();
}

// Time tick — fires every minute, and immediately on registration (first paint).
watch.addEventListener("minutechange", drawScreen);