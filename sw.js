// Speichert die Seite beim ersten Aufruf, danach geht sie auch ohne Netz.
// Bei Änderungen an der Seite die Versionsnummer erhöhen.
const CACHE = "obd-live-1";
const DATEIEN = ["./", "index.html", "manifest.webmanifest", "icon.svg"];

self.addEventListener("install", e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(DATEIEN)));
  self.skipWaiting();
});

self.addEventListener("activate", e => {
  e.waitUntil(caches.keys().then(keys =>
    Promise.all(keys.filter(k => k != CACHE).map(k => caches.delete(k)))));
  self.clients.claim();
});

// erst Netz (damit Änderungen ankommen), ohne Netz aus dem Speicher
self.addEventListener("fetch", e => {
  e.respondWith(
    fetch(e.request)
      .then(antwort => {
        const kopie = antwort.clone();
        caches.open(CACHE).then(c => c.put(e.request, kopie));
        return antwort;
      })
      .catch(() => caches.match(e.request))
  );
});
