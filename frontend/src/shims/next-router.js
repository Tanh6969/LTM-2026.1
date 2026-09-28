import { useState, useEffect } from 'react';

const eventListeners = new Map();

export const events = {
  on(event, handler) {
    if (!eventListeners.has(event)) {
      eventListeners.set(event, new Set());
    }
    eventListeners.get(event).add(handler);
  },
  off(event, handler) {
    if (eventListeners.has(event)) {
      eventListeners.get(event).delete(handler);
    }
  },
  emit(event, ...args) {
    if (eventListeners.has(event)) {
      eventListeners.get(event).forEach((handler) => {
        try {
          handler(...args);
        } catch (e) {
          console.error(e);
        }
      });
    }
  },
};

export function useRouter() {
  const [pathname, setPathname] = useState(
    typeof window !== 'undefined' ? window.location.pathname : '/'
  );

  useEffect(() => {
    const onLocationChange = () => {
      setPathname(window.location.pathname);
      events.emit('routeChangeComplete', window.location.pathname);
    };
    window.addEventListener('popstate', onLocationChange);
    return () => window.removeEventListener('popstate', onLocationChange);
  }, []);

  const searchParams = typeof window !== 'undefined' ? new URLSearchParams(window.location.search) : new URLSearchParams();
  const query = Object.fromEntries(searchParams.entries());

  return {
    pathname,
    query,
    asPath: pathname + (typeof window !== 'undefined' ? window.location.search : ''),
    events,
    push: (url) => {
      events.emit('routeChangeStart', url);
      if (typeof url === 'object') {
        const qs = url.query ? '?' + new URLSearchParams(url.query).toString() : '';
        const target = (url.pathname || '/') + qs;
        window.history.pushState({}, '', target);
        setPathname(url.pathname || '/');
        events.emit('routeChangeComplete', target);
      } else {
        window.history.pushState({}, '', url);
        setPathname(url);
        events.emit('routeChangeComplete', url);
      }
      window.dispatchEvent(new PopStateEvent('popstate'));
    },
    replace: (url) => {
      events.emit('routeChangeStart', url);
      const target = typeof url === 'string' ? url : (url.pathname || '/');
      window.history.replaceState({}, '', target);
      setPathname(target);
      events.emit('routeChangeComplete', target);
      window.dispatchEvent(new PopStateEvent('popstate'));
    },
    back: () => window.history.back(),
  };
}

export default { useRouter, events };
