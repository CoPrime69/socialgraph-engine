/**
 * Product mark: three people and the connections between them, drawn in ink like the graph.
 * Decorative (the product name is always next to it).
 */
export function Logo({ size = 26 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 32 32" aria-hidden="true" focusable="false">
      <g stroke="currentColor" strokeWidth="2" strokeLinecap="round">
        <line x1="8" y1="9" x2="24" y2="12" />
        <line x1="8" y1="9" x2="14" y2="25" />
        <line x1="24" y1="12" x2="14" y2="25" />
      </g>
      <circle cx="8" cy="9" r="4.5" fill="currentColor" />
      <circle cx="24" cy="12" r="3.25" fill="currentColor" />
      <circle
        cx="14"
        cy="25"
        r="4"
        fill="var(--color-page)"
        stroke="currentColor"
        strokeWidth="2.5"
      />
    </svg>
  );
}
