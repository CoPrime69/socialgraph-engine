import { useSyncExternalStore } from 'react';

function subscribe(query: string, onChange: () => void): () => void {
  const list = window.matchMedia(query);
  list.addEventListener('change', onChange);
  return () => {
    list.removeEventListener('change', onChange);
  };
}

/** Live result of a CSS media query. */
export function useMediaQuery(query: string): boolean {
  return useSyncExternalStore(
    (onChange) => subscribe(query, onChange),
    () => window.matchMedia(query).matches,
    () => false,
  );
}

export function usePrefersReducedMotion(): boolean {
  return useMediaQuery('(prefers-reduced-motion: reduce)');
}

export function usePrefersDarkScheme(): boolean {
  return useMediaQuery('(prefers-color-scheme: dark)');
}
