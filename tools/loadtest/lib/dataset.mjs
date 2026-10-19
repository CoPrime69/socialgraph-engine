// Samplers over the real dataset, so the load test asks for the profiles, questions and tags that
// real users would: popular members and questions are requested more often (weighted by views),
// the long tail still appears.
import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import path from 'node:path';

// RFC 4180 parser (quoted fields may contain commas, quotes and newlines). Yields header-keyed rows.
export function* parseCsv(text) {
  let header = null;
  let row = [];
  let field = '';
  let quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"') {
        if (text[i + 1] === '"') { field += '"'; i++; } else { quoted = false; }
      } else {
        field += c;
      }
    } else if (c === '"') {
      quoted = true;
    } else if (c === ',') {
      row.push(field); field = '';
    } else if (c === '\n' || c === '\r') {
      if (c === '\r' && text[i + 1] === '\n') i++;
      row.push(field); field = '';
      if (!header) { header = row; } else { yield Object.fromEntries(header.map((h, k) => [h, row[k]])); }
      row = [];
    } else {
      field += c;
    }
  }
  if (field !== '' || row.length > 0) {
    row.push(field);
    if (header) yield Object.fromEntries(header.map((h, k) => [h, row[k]]));
  }
}

export function readTable(dir, name) {
  return [...parseCsv(gunzipSync(readFileSync(path.join(dir, `${name}.csv.gz`))).toString('utf8'))];
}

// Weighted sampling in O(log n) via a cumulative-weight table.
export class WeightedSampler {
  constructor(items, weight) {
    this.items = items;
    this.cumulative = new Float64Array(items.length);
    let total = 0;
    items.forEach((item, i) => { total += Math.max(weight(item), 0); this.cumulative[i] = total; });
    this.total = total;
  }

  next() {
    const target = Math.random() * this.total;
    let lo = 0;
    let hi = this.cumulative.length - 1;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (this.cumulative[mid] < target) lo = mid + 1; else hi = mid;
    }
    return this.items[lo];
  }
}

// Compact sample of the dataset handed to every worker process.
export function loadSamples(dir) {
  const users = readTable(dir, 'users').map((u) => ({
    id: Number(u.id),
    name: u.display_name,
    views: Number(u.views) || 0,
  }));
  const questions = readTable(dir, 'questions').map((q) => ({
    id: Number(q.id),
    title: q.title,
    views: Number(q.view_count) || 0,
  }));
  const tags = readTable(dir, 'tags').map((t) => ({ name: t.name, count: Number(t.question_count) || 0 }));
  return { users, questions, tags };
}

const STOP_WORDS = new Set(['the', 'and', 'for', 'with', 'how', 'what', 'from', 'into', 'when', 'does', 'why', 'can', 'are', 'there']);

export function buildSamplers({ users, questions, tags }) {
  // sqrt dampens the head so the top handful of members do not dominate every request.
  const profile = new WeightedSampler(users, (u) => Math.sqrt(u.views + 1));
  const question = new WeightedSampler(questions, (q) => Math.sqrt(q.views + 1));
  const tag = new WeightedSampler(tags, (t) => t.count);
  return {
    userId: () => profile.next().id,
    questionId: () => question.next().id,
    tag: () => tag.next().name,
    tagPrefix: () => {
      const name = tag.next().name;
      return name.slice(0, Math.min(name.length, 1 + Math.floor(Math.random() * 3)));
    },
    nameQuery: () => {
      // What someone types into the member search box: the start of a real name.
      const words = profile.next().name.split(/\s+/).filter((w) => w.length >= 3);
      const word = words.length ? words[Math.floor(Math.random() * words.length)] : 'jon';
      return word.slice(0, 3 + Math.floor(Math.random() * 3)).toLowerCase();
    },
    searchText: () => {
      const words = question.next().title.toLowerCase().split(/[^a-z0-9+#]+/).filter((w) => w.length >= 3 && !STOP_WORDS.has(w));
      if (words.length === 0) return 'array';
      const start = Math.floor(Math.random() * words.length);
      return words.slice(start, start + 1 + Math.floor(Math.random() * 2)).join(' ');
    },
  };
}
