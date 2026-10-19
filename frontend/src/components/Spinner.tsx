import styles from './Spinner.module.css';

interface SpinnerProps {
  size?: number;
  /** When set, the spinner is announced; otherwise it is decorative. */
  label?: string;
}

export function Spinner({ size = 16, label }: SpinnerProps) {
  return (
    <span
      className={styles.spinner}
      style={{ width: size, height: size }}
      role={label ? 'status' : undefined}
      aria-label={label}
      aria-hidden={label ? undefined : true}
    />
  );
}
