import {
  forwardRef,
  useId,
  type InputHTMLAttributes,
  type ReactNode,
  type SelectHTMLAttributes,
  type TextareaHTMLAttributes,
} from 'react';
import { describedBy } from './fieldAria';
import styles from './Field.module.css';

interface FieldShellProps {
  id: string;
  label: ReactNode;
  hint?: ReactNode;
  error?: string | undefined;
  optional?: boolean;
  /** Content rendered to the right of the label (e.g. a character counter). */
  aside?: ReactNode;
  children: ReactNode;
  className?: string | undefined;
}

export function FieldShell({
  id,
  label,
  hint,
  error,
  optional,
  aside,
  children,
  className,
}: FieldShellProps) {
  return (
    <div className={[styles.field, className].filter(Boolean).join(' ')}>
      <div className={styles.labelRow}>
        <label htmlFor={id} className={styles.label}>
          {label}
          {optional ? <span className={styles.optional}> (optional)</span> : null}
        </label>
        {aside}
      </div>
      {children}
      {hint ? (
        <p id={`${id}-hint`} className={styles.hint}>
          {hint}
        </p>
      ) : null}
      {error ? (
        <p id={`${id}-error`} className={styles.error}>
          {error}
        </p>
      ) : null}
    </div>
  );
}

interface TextFieldProps extends Omit<InputHTMLAttributes<HTMLInputElement>, 'id'> {
  label: ReactNode;
  hint?: ReactNode;
  error?: string | undefined;
  optional?: boolean;
  aside?: ReactNode;
  /** Element rendered inside the input frame, after the input (e.g. a show-password toggle). */
  trailing?: ReactNode;
  fieldClassName?: string;
}

export const TextField = forwardRef<HTMLInputElement, TextFieldProps>(function TextField(
  { label, hint, error, optional, aside, trailing, fieldClassName, className, ...rest },
  ref,
) {
  const id = useId();
  return (
    <FieldShell
      id={id}
      label={label}
      hint={hint}
      error={error}
      optional={optional}
      aside={aside}
      className={fieldClassName}
    >
      <div className={[styles.control, trailing ? styles.withTrailing : ''].join(' ')}>
        <input
          ref={ref}
          id={id}
          className={[styles.input, className].filter(Boolean).join(' ')}
          {...describedBy(id, hint, error)}
          {...rest}
        />
        {trailing ? <div className={styles.trailing}>{trailing}</div> : null}
      </div>
    </FieldShell>
  );
});

interface SelectFieldProps extends Omit<SelectHTMLAttributes<HTMLSelectElement>, 'id'> {
  label: ReactNode;
  hint?: ReactNode;
  error?: string | undefined;
  optional?: boolean;
  fieldClassName?: string;
  children: ReactNode;
}

export const SelectField = forwardRef<HTMLSelectElement, SelectFieldProps>(function SelectField(
  { label, hint, error, optional, fieldClassName, className, children, ...rest },
  ref,
) {
  const id = useId();
  return (
    <FieldShell
      id={id}
      label={label}
      hint={hint}
      error={error}
      optional={optional}
      className={fieldClassName}
    >
      <select
        ref={ref}
        id={id}
        className={[styles.input, styles.select, className].filter(Boolean).join(' ')}
        {...describedBy(id, hint, error)}
        {...rest}
      >
        {children}
      </select>
    </FieldShell>
  );
});

interface TextAreaFieldProps extends Omit<TextareaHTMLAttributes<HTMLTextAreaElement>, 'id'> {
  label: ReactNode;
  hint?: ReactNode;
  error?: string | undefined;
  optional?: boolean;
  aside?: ReactNode;
  fieldClassName?: string;
}

export const TextAreaField = forwardRef<HTMLTextAreaElement, TextAreaFieldProps>(
  function TextAreaField(
    { label, hint, error, optional, aside, fieldClassName, className, ...rest },
    ref,
  ) {
    const id = useId();
    return (
      <FieldShell
        id={id}
        label={label}
        hint={hint}
        error={error}
        optional={optional}
        aside={aside}
        className={fieldClassName}
      >
        <textarea
          ref={ref}
          id={id}
          className={[styles.input, styles.textarea, className].filter(Boolean).join(' ')}
          {...describedBy(id, hint, error)}
          {...rest}
        />
      </FieldShell>
    );
  },
);
