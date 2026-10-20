/** aria-describedby/aria-invalid for a control rendered inside FieldShell. */
export function describedBy(id: string, hint: unknown, error: string | undefined) {
  const ids = [hint ? `${id}-hint` : null, error ? `${id}-error` : null].filter(Boolean);
  return {
    'aria-describedby': ids.length > 0 ? ids.join(' ') : undefined,
    'aria-invalid': error ? true : undefined,
  };
}
