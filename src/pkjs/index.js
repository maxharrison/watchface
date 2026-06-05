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
});
