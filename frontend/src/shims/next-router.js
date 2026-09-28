import { useState, useEffect } from 'react';

export function useRouter() {
  const [pathname, setPathname] = useState(
    typeof window !== 'undefined' ? window.location.pathname : '/'
  );

  useEffect(() => {
    const onLocationChange = () => {
      setPathname(window.location.pathname);
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
    push: (url) => {
      if (typeof url === 'object') {
        const qs = url.query ? '?' + new URLSearchParams(url.query).toString() : '';
        const target = (url.pathname || '/') + qs;
        window.history.pushState({}, '', target);
      } else {
        window.history.pushState({}, '', url);
      }
      window.dispatchEvent(new PopStateEvent('popstate'));
    },
    replace: (url) => {
      const target = typeof url === 'string' ? url : (url.pathname || '/');
      window.history.replaceState({}, '', target);
      window.dispatchEvent(new PopStateEvent('popstate'));
    },
    back: () => window.history.back(),
  };
}

export default { useRouter };
