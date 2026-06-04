const moddableProxy = require("@moddable/pebbleproxy");
Pebble.addEventListener('ready', moddableProxy.readyReceived);
Pebble.addEventListener('appmessage', moddableProxy.appMessageReceived);

// Forward HEART_RATE_BPM back to watch so Alloy Message module can consume it.
Pebble.addEventListener('appmessage', function(e) {
  if (!e || !e.payload || e.payload.HEART_RATE_BPM === undefined) return;
  Pebble.sendAppMessage(
    { HEART_RATE_BPM: e.payload.HEART_RATE_BPM },
    function() {},
    function(err) { console.log('relay failed: ' + JSON.stringify(err)); }
  );
});
