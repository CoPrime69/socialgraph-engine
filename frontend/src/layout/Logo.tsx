/** Product mark: three connected nodes. Decorative (the product name is always next to it). */
export function Logo({ size = 28 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 32 32" aria-hidden="true" focusable="false">
      <rect width="32" height="32" rx="8" fill="var(--color-accent)" />
      <g stroke="var(--color-accent-contrast)" strokeWidth="2" strokeLinecap="round">
        <line x1="10" y1="11" x2="22" y2="9" />
        <line x1="10" y1="11" x2="15" y2="23" />
        <line x1="22" y1="9" x2="15" y2="23" />
      </g>
      <circle cx="10" cy="11" r="3.5" fill="var(--color-accent-contrast)" />
      <circle cx="22" cy="9" r="3" fill="var(--color-accent-contrast)" />
      <circle cx="15" cy="23" r="4" fill="var(--color-accent-contrast)" />
    </svg>
  );
}
