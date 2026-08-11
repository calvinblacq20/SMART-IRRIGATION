/**
 * Service Worker for Smart Irrigation System PWA
 * Handles application shell caching for offline access.
 */

const CACHE_NAME = 'smart-irrigation-v1';

// Static assets forming the application shell
const ASSETS_TO_CACHE = [
  './',
  './index.html',
  './styles.css',
  './app.js',
  './manifest.json'
];

// Install Event: Cache Application Shell
self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then((cache) => {
        console.log('[Service Worker] Caching Application Shell');
        return cache.addAll(ASSETS_TO_CACHE);
      })
      .then(() => self.skipWaiting())
  );
});

// Activate Event: Clean Up Deprecated Caches
self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((cacheNames) => {
      return Promise.all(
        cacheNames.map((cacheName) => {
          if (cacheName !== CACHE_NAME) {
            console.log('[Service Worker] Deleting old cache version:', cacheName);
            return caches.delete(cacheName);
          }
        })
      );
    }).then(() => self.clients.claim())
  );
});

// Fetch Event: Cache-First for static assets, Network-Only for ESP8266 REST API calls
self.addEventListener('fetch', (event) => {
  const requestUrl = new URL(event.request.url);

  // IMPORTANT: Do NOT cache POST commands or live ESP8266 API requests (/api/)
  if (event.request.method !== 'GET' || requestUrl.pathname.includes('/api/')) {
    event.respondWith(fetch(event.request));
    return;
  }

  // Stale-while-revalidate / Cache-First strategy for static application shell
  event.respondWith(
    caches.match(event.request).then((cachedResponse) => {
      if (cachedResponse) {
        // Fetch updated asset in background
        fetch(event.request).then((networkResponse) => {
          if (networkResponse && networkResponse.status === 200) {
            caches.open(CACHE_NAME).then((cache) => {
              cache.put(event.request, networkResponse);
            });
          }
        }).catch(() => {
          // Ignore network errors while offline
        });
        return cachedResponse;
      }

      // Fallback to network if asset not cached
      return fetch(event.request);
    })
  );
});
