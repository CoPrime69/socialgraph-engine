import { describe, expect, it } from 'vitest';
import {
  formatCompact,
  formatDate,
  formatDateTime,
  formatDecimal,
  formatInteger,
  formatMetricScore,
  formatMonthYear,
  formatPercent,
  formatPercentile,
  formatRelativeTime,
  formatSignificant,
  ordinalRank,
  pluralize,
} from './format';

describe('number formatting', () => {
  it('formats integers, decimals and percentages', () => {
    expect(formatInteger(18649)).toBe('18,649');
    expect(formatInteger(Number.NaN)).toBe('–');
    expect(formatDecimal(0.47123, 2)).toBe('0.47');
    expect(formatDecimal(Number.POSITIVE_INFINITY, 2)).toBe('–');
    expect(formatPercent(0.7234)).toBe('72%');
    expect(formatPercent(0.99567, 1)).toBe('99.6%');
    expect(formatPercent(Number.NaN)).toBe('–');
  });

  it('compacts only large figures', () => {
    expect(formatCompact(18649)).toBe('18,649');
    expect(formatCompact(204670)).toBe('204.7K');
    expect(formatCompact(Number.NaN)).toBe('–');
  });

  it('keeps significant digits for small scores', () => {
    expect(formatSignificant(0.00382)).toBe('0.00382');
    expect(formatSignificant(0.000012345)).toBe('1.23e-5');
    expect(formatSignificant(0)).toBe('0');
    expect(formatSignificant(Number.NaN)).toBe('–');
  });

  it('shows degree centrality as a percentage and other metrics as scores', () => {
    expect(formatMetricScore('degree', 0.0647)).toBe('6.47%');
    expect(formatMetricScore('degree', 0.25)).toBe('25.0%');
    expect(formatMetricScore('degree', 1207)).toBe('1,207');
    expect(formatMetricScore('pagerank', 0.00382)).toBe('0.00382');
  });

  it('formats reputation percentiles with ordinals', () => {
    expect(formatPercentile(100)).toBe('100th percentile');
    expect(formatPercentile(99.987)).toBe('99.98th percentile');
    expect(formatPercentile(51.2)).toBe('51st percentile');
    expect(formatPercentile(42)).toBe('42nd percentile');
    expect(formatPercentile(3)).toBe('3rd percentile');
    expect(formatPercentile(-5)).toBe('0th percentile');
    expect(formatPercentile(Number.NaN)).toBe('–');
    expect(ordinalRank(1204)).toBe('#1,204');
  });

  it('pluralizes counts', () => {
    expect(pluralize(1, 'person', 'people')).toBe('1 person');
    expect(pluralize(1204, 'person', 'people')).toBe('1,204 people');
    expect(pluralize(2, 'view')).toBe('2 views');
  });
});

describe('date formatting', () => {
  it('formats dates in the viewer locale', () => {
    expect(formatDate('2008-09-29T05:46:02Z')).toBe('Sep 29, 2008');
    expect(formatMonthYear('2008-09-29T05:46:02Z')).toBe('Sep 2008');
    expect(formatDateTime('2008-09-29T05:46:02Z')).toBe('Sep 29, 2008, 5:46 AM');
    expect(formatDate(null)).toBe('–');
    expect(formatMonthYear('not a date')).toBe('–');
    expect(formatDateTime(undefined)).toBe('–');
  });

  it('describes times relative to now', () => {
    const now = Date.parse('2026-10-19T12:00:00Z');
    expect(formatRelativeTime('2026-10-19T11:59:58Z', now)).toBe('just now');
    expect(formatRelativeTime('2026-10-19T11:59:00Z', now)).toBe('1 minute ago');
    expect(formatRelativeTime('2026-10-19T09:00:00Z', now)).toBe('3 hours ago');
    expect(formatRelativeTime('2026-10-18T12:00:00Z', now)).toBe('yesterday');
    expect(formatRelativeTime('2026-10-19T15:00:00Z', now)).toBe('in 3 hours');
    expect(formatRelativeTime('2008-09-29T05:46:02Z', now)).toBe('18 years ago');
    expect(formatRelativeTime(null, now)).toBe('–');
  });
});
