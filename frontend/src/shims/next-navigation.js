import { useRouter } from './next-router';

export { useRouter };
export function usePathname() {
  return typeof window !== 'undefined' ? window.location.pathname : '/';
}
export function useSearchParams() {
  return typeof window !== 'undefined' ? new URLSearchParams(window.location.search) : new URLSearchParams();
}
export default { useRouter, usePathname, useSearchParams };
