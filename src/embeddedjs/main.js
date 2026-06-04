import Poco from "commodetto/Poco";
import Battery from "embedded:sensor/Battery";
import Location from "embedded:sensor/Location";

const render = new Poco(screen);

// ---- Fonts ----
const timeFont  = new render.Font("Bitham-Bold", 42);
const smallFont = new render.Font("Gothic-Bold", 18);

// ---- Colors (matched to the mockup) ----
const black   = render.makeColor(0, 0, 0);
const white   = render.makeColor(255, 255, 255);
const dim     = render.makeColor(138, 138, 138); // #8a8a8a
const orange  = render.makeColor(255, 170, 0);   // #ffaa00
const stripBg = render.makeColor(20, 20, 20);    // #141414
const green   = render.makeColor(0, 210, 106);   // #00d26a
const yellow  = render.makeColor(255, 170, 0);
const red     = render.makeColor(255, 0, 0);

// ---- Lowercase date names ("wed 03 jun") ----
const DAYS   = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"];
const MONTHS = ["jan", "feb", "mar", "apr", "may", "jun",
                "jul", "aug", "sep", "oct", "nov", "dec"];

// ---- State ----
let lastDate = new Date();
let batteryPercent = 100;
let weather = null;

// Placeholders (no Alloy APIs for these yet)
const HEART_RATE = 72;            // placeholder
const NEXT_EVENT = "15:00 standup"; // calendar placeholder

// ---- Battery (live) ----
const battery = new Battery({
    onSample() {
        batteryPercent = this.sample().percent;
        drawScreen();
    }
});
batteryPercent = battery.sample().percent;

// ---- Drawing ----
function drawScreen(event) {
    const now = event?.date ?? lastDate;
    if (event?.date) lastDate = event.date;

    const W = render.width;
    const H = render.height;
    const pad = 12;

    render.begin();
    render.fillRectangle(black, 0, 0, W, H);

    // --- Battery bar: 4px strip across the very top, width = percent ---
    let barColor = green;
    if (batteryPercent <= 20) barColor = red;
    else if (batteryPercent <= 40) barColor = yellow;
    render.fillRectangle(barColor, 0, 0, ((batteryPercent * W) / 100) | 0, 4);

    // --- Top-left: heart rate, "72" orange + " hr" dim ---
    const hrVal = String(HEART_RATE);
    render.drawText(hrVal, smallFont, orange, pad, 12);
    render.drawText(" hr", smallFont, dim,
        pad + render.getTextWidth(hrVal, smallFont), 12);

    // --- Top-right: weather, "18°" white + " cldy" dim ---
    if (weather) {
        const tempStr = `${weather.temp}°`;
        const condStr = ` ${weather.conditions}`;
        const total = render.getTextWidth(tempStr, smallFont) +
                      render.getTextWidth(condStr, smallFont);
        const wx = W - pad - total;
        render.drawText(tempStr, smallFont, white, wx, 12);
        render.drawText(condStr, smallFont, dim,
            wx + render.getTextWidth(tempStr, smallFont), 12);
    } else {
        const msg = "...";
        render.drawText(msg, smallFont, dim,
            W - pad - render.getTextWidth(msg, smallFont), 12);
    }

    // --- Center block at ~44% height: time + date ---
    const centerY = (H * 0.44) | 0;

    const hours = String(now.getHours()).padStart(2, "0");
    const minutes = String(now.getMinutes()).padStart(2, "0");
    const timeStr = `${hours}:${minutes}`;
    let width = render.getTextWidth(timeStr, timeFont);
    const timeY = centerY - ((timeFont.height / 2) | 0) - 8;
    render.drawText(timeStr, timeFont, white, ((W - width) / 2) | 0, timeY);

    const dateStr = `${DAYS[now.getDay()]} ` +
        `${String(now.getDate()).padStart(2, "0")} ${MONTHS[now.getMonth()]}`;
    width = render.getTextWidth(dateStr, smallFont);
    render.drawText(dateStr, smallFont, dim,
        ((W - width) / 2) | 0, timeY + timeFont.height + 7);

    // --- Bottom strip: calendar placeholder, orange on dark grey ---
    const stripH = smallFont.height + 14;
    render.fillRectangle(stripBg, 0, H - stripH, W, stripH);
    render.drawText(NEXT_EVENT, smallFont, orange, pad, H - stripH + 7);

    render.end();
}

// ---- Weather via Location + Open-Meteo ----
function shortConditions(code) {
    if (code === 0) return "clr";
    if (code <= 3) return "cldy";
    if (code <= 48) return "fog";
    if (code <= 57) return "drzl";
    if (code <= 67) return "rain";
    if (code <= 77) return "snow";
    if (code <= 86) return "shwr";
    if (code <= 99) return "storm";
    return "?";
}

async function fetchWeather(latitude, longitude) {
    try {
        const url = new URL("http://api.open-meteo.com/v1/forecast");
        url.search = new URLSearchParams({
            latitude,
            longitude,
            current: "temperature_2m,weather_code"
        });
        const response = await fetch(url);
        const data = await response.json();
        weather = {
            temp: Math.round(data.current.temperature_2m),
            conditions: shortConditions(data.current.weather_code)
        };
        drawScreen();
    } catch (e) {
        console.log("Weather fetch error: " + e);
    }
}

function requestLocation() {
    new Location({
        onSample() {
            const s = this.sample();
            this.close(); // one-shot: close after first reading
            fetchWeather(s.latitude, s.longitude);
        }
    });
}

// ---- Events ----
watch.addEventListener("minutechange", drawScreen);   // fires immediately too
watch.addEventListener("hourchange", requestLocation); // hourly weather refresh