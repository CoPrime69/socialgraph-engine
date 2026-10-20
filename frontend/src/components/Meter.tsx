import styles from './Meter.module.css';

interface MeterProps {
  /** Value in [0, max]. */
  value: number;
  max?: number;
  /** Accessible name, e.g. "Connection strength". */
  label: string;
  /** Text shown next to the bar and used as aria-valuetext, e.g. "72%". */
  valueText: string;
  /** Hide the visible value text (it is still announced). */
  hideValue?: boolean;
  size?: 'sm' | 'md';
}

/** Horizontal meter: single-hue fill on a lighter track of the same ramp; the value is always also text. */
export function Meter({
  value,
  max = 1,
  label,
  valueText,
  hideValue = false,
  size = 'md',
}: MeterProps) {
  const safeMax = max > 0 ? max : 1;
  const ratio = Math.min(1, Math.max(0, Number.isFinite(value) ? value / safeMax : 0));
  return (
    <div className={`${styles.meter} ${styles[size]}`}>
      <div
        className={styles.track}
        role="meter"
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={safeMax}
        aria-valuenow={Math.min(safeMax, Math.max(0, value))}
        aria-valuetext={valueText}
      >
        <span className={styles.fill} style={{ width: `${(ratio * 100).toFixed(1)}%` }} />
      </div>
      {hideValue ? null : (
        <span className={styles.value} aria-hidden="true">
          {valueText}
        </span>
      )}
    </div>
  );
}
