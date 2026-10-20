import type { InfluenceMetric } from '../api/types';

/**
 * Number and date formatting in the viewer's locale. Large standalone figures are compacted
 * (18,649 → 18.6K) only from 100,000 upwards so exact counts stay readable.
 */

const integerFormat = new Intl.NumberFormat(undefined, { maximumFractionDigits: 0 });
const compactFormat = new Intl.NumberFormat(undefined, {
  notation: 'compact',
  maximumFractionDigits: 1,
});

export function formatInteger(value: number): string {
  return Number.isFinite(value) ? integerFormat.format(value) : 'n/a';
}

export function formatCompact(value: number, threshold = 100_000): string {
  if (!Number.isFinite(value)) return 'n/a';
  return Math.abs(value) >= threshold ? compactFormat.format(value) : integerFormat.format(value);
}

export function formatDecimal(value: number, fractionDigits: number): string {
  if (!Number.isFinite(value)) return 'n/a';
  return new Intl.NumberFormat(undefined, {
    minimumFractionDigits: fractionDigits,
    maximumFractionDigits: fractionDigits,
  }).format(value);
}

/** 0.7234 → "72%"; values are ratios in [0, 1]. */
export function formatPercent(ratio: number, fractionDigits = 0): string {
  if (!Number.isFinite(ratio)) return 'n/a';
  return new Intl.NumberFormat(undefined, {
    style: 'percent',
    minimumFractionDigits: fractionDigits,
    maximumFractionDigits: fractionDigits,
  }).format(ratio);
}

/** Small scores (PageRank ≈ 1e-4) need significant digits rather than fixed decimals. */
export function formatSignificant(value: number, digits = 3): string {
  if (!Number.isFinite(value)) return 'n/a';
  if (value === 0) return '0';
  if (Math.abs(value) < 1e-4) return value.toExponential(digits - 1);
  return new Intl.NumberFormat(undefined, {
    maximumSignificantDigits: digits,
  }).format(value);
}

/**
 * Formats an influence score. Degree arrives as degree centrality (the fraction of all other
 * people someone is connected to) and is shown as a percentage; a raw count is shown as is.
 */
export function formatMetricScore(metric: InfluenceMetric, score: number): string {
  if (metric === 'degree') {
    if (score > 1 && Number.isInteger(score)) return formatInteger(score);
    return formatPercent(score, score < 0.1 ? 2 : 1);
  }
  return formatSignificant(score, 3);
}

const ordinalRules = new Intl.PluralRules('en-US', { type: 'ordinal' });
const ORDINAL_SUFFIX: Partial<Record<Intl.LDMLPluralRule, string>> = {
  one: 'st',
  two: 'nd',
  few: 'rd',
};

/**
 * Reputation percentile (the profile's `influence`, 0-100) → "97th percentile".
 * Above 99 two decimals are kept, because that is where the most active users differ.
 */
export function formatPercentile(influence: number): string {
  if (!Number.isFinite(influence)) return 'n/a';
  const clamped = Math.min(100, Math.max(0, influence));
  const shown =
    clamped >= 99 && clamped < 100 ? Math.floor(clamped * 100) / 100 : Math.floor(clamped);
  const suffix = Number.isInteger(shown)
    ? (ORDINAL_SUFFIX[ordinalRules.select(shown)] ?? 'th')
    : 'th';
  return `${String(shown)}${suffix} percentile`;
}

export function ordinalRank(rank: number): string {
  return `#${formatInteger(rank)}`;
}

function toDate(iso: string | null | undefined): Date | null {
  if (!iso) return null;
  const date = new Date(iso);
  return Number.isNaN(date.getTime()) ? null : date;
}

const dateFormat = new Intl.DateTimeFormat(undefined, { dateStyle: 'medium' });
const monthYearFormat = new Intl.DateTimeFormat(undefined, { month: 'short', year: 'numeric' });
const dateTimeFormat = new Intl.DateTimeFormat(undefined, {
  dateStyle: 'medium',
  timeStyle: 'short',
});

/** "Oct 19, 2026" */
export function formatDate(iso: string | null | undefined): string {
  const date = toDate(iso);
  return date ? dateFormat.format(date) : 'n/a';
}

/** "Sep 2008" */
export function formatMonthYear(iso: string | null | undefined): string {
  const date = toDate(iso);
  return date ? monthYearFormat.format(date) : 'n/a';
}

export function formatDateTime(iso: string | null | undefined): string {
  const date = toDate(iso);
  return date ? dateTimeFormat.format(date) : 'n/a';
}

const relativeFormat = new Intl.RelativeTimeFormat(undefined, { numeric: 'auto' });
const RELATIVE_STEPS: readonly [Intl.RelativeTimeFormatUnit, number][] = [
  ['second', 60],
  ['minute', 60],
  ['hour', 24],
  ['day', 30],
  ['month', 12],
  ['year', Number.POSITIVE_INFINITY],
];

/** "2 minutes ago", "yesterday", "in 3 hours". */
export function formatRelativeTime(
  iso: string | null | undefined,
  now: number = Date.now(),
): string {
  const date = toDate(iso);
  if (!date) return 'n/a';
  let value = (date.getTime() - now) / 1000;
  for (const [unit, size] of RELATIVE_STEPS) {
    if (Math.abs(value) < size) {
      if (unit === 'second' && Math.abs(value) < 10) return 'just now';
      return relativeFormat.format(Math.round(value), unit);
    }
    value /= size;
  }
  return relativeFormat.format(Math.round(value), 'year');
}

export function pluralize(count: number, singular: string, plural = `${singular}s`): string {
  return `${formatInteger(count)} ${count === 1 ? singular : plural}`;
}
