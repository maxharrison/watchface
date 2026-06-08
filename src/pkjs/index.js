function shortConditions(code) {
  if (code === 0)  return 'clr';
  if (code <= 3)   return 'cldy';
  if (code <= 48)  return 'fog';
  if (code <= 57)  return 'drzl';
  if (code <= 67)  return 'rain';
  if (code <= 77)  return 'snow';
  if (code <= 86)  return 'shwr';
  if (code <= 99)  return 'storm';
  return '?';
}

function fetchWeather(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat +
    '&longitude=' + lon +
    '&current=temperature_2m,weather_code';

  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    try {
      var data = JSON.parse(this.responseText);
      var temp = Math.round(data.current.temperature_2m);
      var cond = shortConditions(data.current.weather_code);
      Pebble.sendAppMessage(
        { 'WEATHER_TEMP_C': temp, 'WEATHER_COND': cond },
        function() { console.log('Weather sent: ' + temp + ' ' + cond); },
        function(e) { console.log('Weather send error: ' + JSON.stringify(e)); }
      );
    } catch (e) {
      console.log('Weather parse error: ' + e);
    }
  };
  xhr.open('GET', url);
  xhr.send();
}

// ── Next calendar event ──────────────────────────────────────────────────────
// The watch has no calendar API, so we fetch the next event from a tiny HTTP
// endpoint on the home server. That endpoint reuses the existing Radicale MCP
// server's client (getCalClient / parseVEvent / expandRruleOccurrences) — see
// the server snippet in the PR description — and returns just:
//   { "event": { "summary": "standup", "start": "2026-06-08T15:00:00Z" } }
// or { "event": null } when nothing is upcoming.
//
// Reachable over Tailscale. Expose it with `tailscale serve` so the MagicDNS
// name gets a real Let's Encrypt cert (the pkjs sandbox rejects self-signed),
// and so it stays tailnet-only (no app-level auth needed).
var CAL_ENDPOINT   = 'https://YOUR-HOST.YOUR-TAILNET.ts.net/next-event.json';
var CAL_REFRESH_MS = 15 * 60 * 1000;  // re-poll every 15 min while the face is up

function pad2(n) { return (n < 10 ? '0' : '') + n; }

function fetchNextEvent() {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    try {
      var data = JSON.parse(this.responseText);
      var label;
      if (data && data.event) {
        var d = new Date(data.event.start);
        label = pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ' ' + data.event.summary;
      } else {
        label = 'no events';
      }
      // The watch strip fits ~31 chars; trim so it can't overflow the C buffer.
      label = label.substring(0, 31);
      Pebble.sendAppMessage(
        { 'CAL_EVENT': label },
        function() { console.log('Calendar sent: ' + label); },
        function(e) { console.log('Calendar send error: ' + JSON.stringify(e)); }
      );
    } catch (e) {
      console.log('Calendar parse error: ' + e);
    }
  };
  xhr.onerror = function() { console.log('Calendar fetch error'); };
  xhr.open('GET', CAL_ENDPOINT);
  xhr.send();
}

Pebble.addEventListener('ready', function() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      fetchWeather(pos.coords.latitude, pos.coords.longitude);
    },
    function(err) {
      console.log('Location error: ' + err.message);
    },
    { timeout: 15000, maximumAge: 300000 }
  );

  fetchNextEvent();
  setInterval(fetchNextEvent, CAL_REFRESH_MS);
});
