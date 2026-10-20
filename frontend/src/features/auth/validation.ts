import type { FieldErrorDetail, RegisterRequest, Taxonomy } from '../../api/types';

/**
 * Client-side validation mirroring docs/contracts.md §3 ("Taxonomy and validation") and
 * §10.4 (registration body). The server remains the authority: its 422 `details` are mapped
 * onto the same fields with `mapServerErrors`.
 */

export const HANDLE_PATTERN = /^[a-z0-9_]{3,32}$/;
export const NAME_MAX_CHARS = 64;
export const PASSWORD_MIN_BYTES = 8;
export const PASSWORD_MAX_BYTES = 128;
export const ABOUT_MAX_CHARS = 500;
/** Not in contracts.md; identity-service accepts at most 100 characters. */
export const LOCATION_MAX_CHARS = 100;

const CONTROL_CHARS = /\p{Cc}/u;
/** Control characters other than line breaks and tabs (allowed in "about"). */
const CONTROL_CHARS_EXCEPT_BREAKS = /(?![\n\r\t])\p{Cc}/u;

export type FieldErrors<F extends string> = Partial<Record<F, string>>;

/** Counts user-perceived code points (not UTF-16 units) so emoji count as one character. */
export function charCount(value: string): number {
  return Array.from(value).length;
}

export function byteLength(value: string): number {
  return new TextEncoder().encode(value).length;
}

/** Handles are stored lower-case; trimming/lower-casing is applied before validation. */
export function normalizeHandle(value: string): string {
  return value.trim().toLowerCase();
}

export function validateHandle(raw: string): string | undefined {
  const value = normalizeHandle(raw);
  if (value === '') return 'Enter a handle.';
  if (!HANDLE_PATTERN.test(value)) {
    return 'Use 3-32 characters: lowercase letters, numbers or underscores.';
  }
  return undefined;
}

export function validateName(raw: string): string | undefined {
  const value = raw.trim();
  if (value === '') return 'Enter your name.';
  if (charCount(value) > NAME_MAX_CHARS) return `Use at most ${NAME_MAX_CHARS} characters.`;
  if (CONTROL_CHARS.test(value)) return 'Remove control characters (such as tabs or line breaks).';
  return undefined;
}

export function validatePassword(value: string): string | undefined {
  if (value === '') return 'Enter a password.';
  const bytes = byteLength(value);
  if (bytes < PASSWORD_MIN_BYTES) return `Use at least ${PASSWORD_MIN_BYTES} characters.`;
  if (bytes > PASSWORD_MAX_BYTES) return `Use at most ${PASSWORD_MAX_BYTES} bytes.`;
  return undefined;
}

export function validateLocation(raw: string): string | undefined {
  const value = raw.trim();
  if (charCount(value) > LOCATION_MAX_CHARS) return `Use at most ${LOCATION_MAX_CHARS} characters.`;
  if (CONTROL_CHARS.test(value)) return 'Remove control characters (such as tabs or line breaks).';
  return undefined;
}

export function validateAbout(raw: string): string | undefined {
  const value = raw.trim();
  if (charCount(value) > ABOUT_MAX_CHARS) return `Use at most ${ABOUT_MAX_CHARS} characters.`;
  if (CONTROL_CHARS_EXCEPT_BREAKS.test(value)) return 'Remove control characters.';
  return undefined;
}

// ---------- Sign in ----------

export type SignInField = 'handle' | 'password';

export interface SignInValues {
  handle: string;
  password: string;
}

export function validateSignIn(values: SignInValues): FieldErrors<SignInField> {
  const errors: FieldErrors<SignInField> = {};
  if (normalizeHandle(values.handle) === '') errors.handle = 'Enter your handle.';
  if (values.password === '') errors.password = 'Enter your password.';
  return errors;
}

// ---------- Registration ----------

export type RegisterField =
  'handle' | 'name' | 'password' | 'category' | 'branch' | 'location' | 'about';

export const REGISTER_FIELDS: readonly RegisterField[] = [
  'handle',
  'name',
  'password',
  'category',
  'branch',
  'location',
  'about',
];

export interface RegisterValues {
  handle: string;
  name: string;
  password: string;
  category: string;
  branch: string;
  location: string;
  about: string;
}

export function validateRegistration(
  values: RegisterValues,
  taxonomy: Taxonomy | undefined,
): FieldErrors<RegisterField> {
  const errors: FieldErrors<RegisterField> = {};
  const set = (field: RegisterField, message: string | undefined) => {
    if (message) errors[field] = message;
  };
  set('handle', validateHandle(values.handle));
  set('name', validateName(values.name));
  set('password', validatePassword(values.password));
  set('location', validateLocation(values.location));
  set('about', validateAbout(values.about));

  if (values.category === '') {
    errors.category = 'Choose a category.';
  } else if (taxonomy && !taxonomy.categories.includes(values.category)) {
    errors.category = 'Choose a category from the list.';
  }

  if (values.branch === '') {
    errors.branch = 'Choose your primary technology.';
  } else if (taxonomy) {
    const owner = taxonomy.branch_categories[values.branch];
    if (!taxonomy.branches.includes(values.branch) || owner === undefined) {
      errors.branch = 'Choose a technology from the list.';
    } else if (values.category !== '' && owner !== values.category) {
      errors.branch = `Choose a technology from ${values.category}.`;
    }
  }
  return errors;
}

/** Builds the request body: trims text, lower-cases the handle and omits empty optionals. */
export function toRegisterRequest(values: RegisterValues): RegisterRequest {
  const location = values.location.trim();
  const about = values.about.trim();
  return {
    handle: normalizeHandle(values.handle),
    name: values.name.trim(),
    password: values.password,
    category: values.category,
    branch: values.branch,
    ...(location ? { location } : {}),
    ...(about ? { about } : {}),
  };
}

// ---------- Server-side errors ----------

/**
 * Maps 422 `details` onto known form fields. Details for unknown fields (or without a field)
 * are returned in `other` so they can be shown in the form-level alert instead of being lost.
 */
export function mapServerErrors<F extends string>(
  details: readonly FieldErrorDetail[],
  fields: readonly F[],
): { fieldErrors: FieldErrors<F>; other: string[] } {
  const fieldErrors: FieldErrors<F> = {};
  const other: string[] = [];
  for (const detail of details) {
    const field = fields.find((candidate) => candidate === detail.field);
    if (field !== undefined && fieldErrors[field] === undefined) {
      fieldErrors[field] = sentence(detail.message);
    } else if (field === undefined) {
      other.push(detail.field ? `${detail.field}: ${detail.message}` : detail.message);
    }
  }
  return { fieldErrors, other };
}

/** "must be 3-32 characters" → "Must be 3-32 characters." */
function sentence(message: string): string {
  const trimmed = message.trim();
  if (trimmed === '') return 'This value is not valid.';
  const capitalised = trimmed.charAt(0).toUpperCase() + trimmed.slice(1);
  return /[.!?]$/.test(capitalised) ? capitalised : `${capitalised}.`;
}
