import { useId } from 'react';
import styles from './SegmentedControl.module.css';

export interface SegmentOption<T extends string> {
  value: T;
  label: string;
}

interface SegmentedControlProps<T extends string> {
  /** Accessible group label (rendered as a visually hidden legend unless `showLabel`). */
  label: string;
  showLabel?: boolean;
  options: readonly SegmentOption<T>[];
  value: T;
  onChange: (value: T) => void;
  size?: 'sm' | 'md';
}

/** A radio group styled as segmented buttons (arrow keys move between options natively). */
export function SegmentedControl<T extends string>({
  label,
  showLabel = false,
  options,
  value,
  onChange,
  size = 'md',
}: SegmentedControlProps<T>) {
  const name = useId();
  return (
    <fieldset className={`${styles.group} ${styles[size]}`}>
      <legend className={showLabel ? styles.legend : 'visually-hidden'}>{label}</legend>
      <div className={styles.options}>
        {options.map((option) => (
          <label key={option.value} className={styles.option}>
            <input
              type="radio"
              name={name}
              value={option.value}
              checked={option.value === value}
              onChange={() => {
                onChange(option.value);
              }}
              className={styles.input}
            />
            <span className={styles.text}>{option.label}</span>
          </label>
        ))}
      </div>
    </fieldset>
  );
}
