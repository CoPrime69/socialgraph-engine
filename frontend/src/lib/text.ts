import type { TagList } from '../api/types';

/** "Christian C. Salvadó" → "CS", "paxdiablo" → "P". Unicode-aware. */
export function initials(name: string, fallback = '?'): string {
  const words = name
    .trim()
    .split(/\s+/)
    .map((word) => Array.from(word.replace(/[^\p{L}\p{N}]/gu, '')))
    .filter((letters) => letters.length > 0);
  const first = words[0]?.[0];
  if (!first) return fallback;
  const last = words.length > 1 ? words[words.length - 1]?.[0] : undefined;
  return (first + (last ?? '')).toUpperCase();
}

/**
 * Returns `url` only if it is an absolute https URL (external links are never rendered for
 * other schemes such as `javascript:`, `data:` or plain `http:`).
 */
export function safeExternalUrl(url: string | null | undefined): string | null {
  if (!url) return null;
  try {
    const parsed = new URL(url.trim());
    return parsed.protocol === 'https:' && parsed.hostname !== '' ? parsed.toString() : null;
  } catch {
    return null;
  }
}

/** Displayable host for a URL ("https://blog.example.com/x" → "blog.example.com"). */
export function displayHost(url: string): string {
  try {
    const parsed = new URL(url);
    return parsed.host.replace(/^www\./, '') + (parsed.pathname === '/' ? '' : parsed.pathname);
  } catch {
    return url;
  }
}

/** Accepts an array, the dataset's space-separated string, or Stack Exchange `<a><b>` syntax. */
export function normalizeTags(tags: TagList | null | undefined): string[] {
  if (!tags) return [];
  if (Array.isArray(tags)) return tags.filter((tag) => typeof tag === 'string' && tag !== '');
  if (tags.includes('<')) {
    return Array.from(tags.matchAll(/<([^>]+)>/g), (match) => match[1] ?? '').filter(Boolean);
  }
  return tags.split(/\s+/).filter(Boolean);
}

/** Truncates on a word boundary and appends an ellipsis. */
export function truncate(text: string, maxLength: number): string {
  if (text.length <= maxLength) return text;
  const slice = text.slice(0, maxLength);
  const lastSpace = slice.lastIndexOf(' ');
  return `${(lastSpace > maxLength * 0.6 ? slice.slice(0, lastSpace) : slice).trimEnd()}…`;
}

const BLOCK_SELECTOR = 'p, div, li, blockquote, pre, h1, h2, h3, h4, h5, h6, tr';
/** Elements whose content is not text a reader would see. */
const HIDDEN_SELECTOR = 'script, style, template, noscript';

/**
 * Plain text from a profile's "about" field, which for imported Stack Overflow users contains
 * HTML. DOMParser builds an inert document (no scripts run, nothing is fetched) and only its
 * text is kept, so no markup from the dataset ever reaches the page. Block boundaries become
 * line breaks.
 */
export function htmlToText(html: string): string {
  if (!/[<&]/.test(html)) return html.trim();
  const doc = new DOMParser().parseFromString(html, 'text/html');
  for (const hidden of Array.from(doc.querySelectorAll(HIDDEN_SELECTOR))) hidden.remove();
  for (const br of Array.from(doc.querySelectorAll('br'))) br.replaceWith('\n');
  for (const block of Array.from(doc.querySelectorAll(BLOCK_SELECTOR))) block.append('\n\n');
  return doc.body.textContent
    .replace(/[ \t]+\n/g, '\n')
    .replace(/\n{3,}/g, '\n\n')
    .trim();
}

/** Splits text into paragraphs on blank lines. */
export function paragraphs(text: string): string[] {
  return text
    .split(/\n\s*\n/)
    .map((paragraph) => paragraph.trim())
    .filter(Boolean);
}
