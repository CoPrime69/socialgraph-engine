import { SERIES_SLOTS } from './palette';

/** Colours for canvas drawing, read from the design tokens (so light/dark stay in sync). */
export interface GraphTheme {
  /** Index 0 = "Other", 1..8 = categorical slots. */
  series: string[];
  edge: string;
  ring: string;
  highlight: string;
  you: string;
  text: string;
  surface: string;
  font: string;
}

const FALLBACK: GraphTheme = {
  series: [
    '#8f8d86',
    '#2a78d6',
    '#eb6834',
    '#1baf7a',
    '#eda100',
    '#e87ba4',
    '#008300',
    '#6250d6',
    '#e34948',
  ],
  edge: '#8f8d86',
  ring: '#fcfcfb',
  highlight: '#0b0b0b',
  you: '#256abf',
  text: '#0b0b0b',
  surface: '#fcfcfb',
  font: 'system-ui, sans-serif',
};

export function readGraphTheme(element: Element): GraphTheme {
  const style = getComputedStyle(element);
  const read = (name: string, fallback: string) => style.getPropertyValue(name).trim() || fallback;
  const series = [read('--series-other', FALLBACK.series[0] ?? '#8f8d86')];
  for (let slot = 1; slot <= SERIES_SLOTS; slot++) {
    series.push(read(`--series-${slot}`, FALLBACK.series[slot] ?? '#8f8d86'));
  }
  return {
    series,
    edge: read('--graph-edge', FALLBACK.edge),
    ring: read('--graph-ring', FALLBACK.ring),
    highlight: read('--graph-highlight', FALLBACK.highlight),
    you: read('--graph-you', FALLBACK.you),
    text: read('--color-text', FALLBACK.text),
    surface: read('--color-surface', FALLBACK.surface),
    font: read('--font-sans', FALLBACK.font),
  };
}
