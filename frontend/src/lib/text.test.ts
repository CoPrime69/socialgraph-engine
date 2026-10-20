import { describe, expect, it } from 'vitest';
import { taxonomy } from '../test/fixtures';
import { branchesFor } from './taxonomy';
import {
  displayHost,
  htmlToText,
  initials,
  normalizeTags,
  paragraphs,
  safeExternalUrl,
  truncate,
} from './text';

describe('initials', () => {
  it('takes the first letters of the first and last words', () => {
    expect(initials('Christian C. Salvadó')).toBe('CS');
    expect(initials('bobince')).toBe('B');
    expect(initials('  émile   zola ')).toBe('ÉZ');
    expect(initials('!!!')).toBe('?');
    expect(initials('', '-')).toBe('-');
  });
});

describe('safeExternalUrl', () => {
  it('accepts absolute https URLs only', () => {
    expect(safeExternalUrl('https://stackoverflow.com/users/22656')).toBe(
      'https://stackoverflow.com/users/22656',
    );
    expect(safeExternalUrl(' https://hewgill.com ')).toBe('https://hewgill.com/');
    expect(safeExternalUrl('http://csharpindepth.com')).toBeNull();
    expect(safeExternalUrl('javascript:alert(1)')).toBeNull();
    expect(safeExternalUrl('data:text/html,hi')).toBeNull();
    expect(safeExternalUrl('/relative')).toBeNull();
    expect(safeExternalUrl(null)).toBeNull();
    expect(safeExternalUrl('')).toBeNull();
  });

  it('shows a readable host', () => {
    expect(displayHost('https://www.doxdesk.com/')).toBe('doxdesk.com');
    expect(displayHost('https://devstory.fyi/vonc')).toBe('devstory.fyi/vonc');
    expect(displayHost('not a url')).toBe('not a url');
  });
});

describe('normalizeTags', () => {
  it('accepts arrays, space-separated strings and <a><b> syntax', () => {
    expect(normalizeTags(['c#', '', '.net'])).toEqual(['c#', '.net']);
    expect(normalizeTags('java constructor')).toEqual(['java', 'constructor']);
    expect(normalizeTags('<c#><.net><string>')).toEqual(['c#', '.net', 'string']);
    expect(normalizeTags(null)).toEqual([]);
    expect(normalizeTags('')).toEqual([]);
  });
});

describe('truncate', () => {
  it('cuts on a word boundary', () => {
    expect(truncate('Short', 10)).toBe('Short');
    expect(truncate('Christian C. Salvadó and friends', 20)).toBe('Christian C. Salvadó…');
    expect(truncate('Supercalifragilistic', 10)).toBe('Supercalif…');
  });
});

describe('htmlToText', () => {
  it('keeps the text of imported HTML and drops every tag', () => {
    const text = htmlToText(
      '<p>Author of <a href="http://csharpindepth.com">C# in Depth</a></p><p>Works at Google<br>remotely</p><script>alert(1)</script><img src=x onerror="alert(2)">',
    );
    expect(text).toBe('Author of C# in Depth\n\nWorks at Google\nremotely');
    expect(text).not.toContain('<');
  });

  it('decodes entities and leaves plain text alone', () => {
    expect(htmlToText('Tom &amp; Jerry')).toBe('Tom & Jerry');
    expect(htmlToText('  plain text  ')).toBe('plain text');
  });

  it('splits paragraphs on blank lines', () => {
    expect(paragraphs('One\n\n  Two\nstill two\n\n\n')).toEqual(['One', 'Two\nstill two']);
  });
});

describe('branchesFor', () => {
  it('lists the branches of a category, or all of them', () => {
    expect(branchesFor(taxonomy, 'Web Development')).toEqual(['javascript', 'php', 'css']);
    expect(branchesFor(taxonomy, '')).toHaveLength(taxonomy.branches.length);
  });
});
