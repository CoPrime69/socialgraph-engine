#!/usr/bin/env node
// Production-style load test: a population of signed-in members using every feature at once.
//
// 1. Setup: registers (or signs in) MEMBERS virtual members, waits until graph-service has
//    provisioned their profiles, and gives each a few real connections.
// 2. Load: PROCESSES autocannon processes keep CONNECTIONS keep-alive connections busy for
//    DURATION seconds. Every request is drawn from the weighted mix in lib/mix.mjs for a random
//    virtual member, with ids, tags and search terms sampled from the real dataset.
//    RATE (requests/s across all processes) switches from "as fast as possible" to a fixed rate,
//    for latency-at-load measurements.
// 3. Report: total throughput, errors, and latency percentiles overall and per operation, printed
//    as a table and saved as JSON + Markdown under tools/loadtest/results/.
//
//   BASE_URL=http://localhost:8080 DURATION=60 CONNECTIONS=64 node tools/loadtest/workload.mjs
import { fork } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import autocannon from 'autocannon';
import * as hdr from 'hdr-histogram-js';
import { buildSamplers, loadSamples } from './lib/dataset.mjs';
import { OPERATIONS, pickOperation } from './lib/mix.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const env = (name, fallback) => process.env[name] ?? fallback;
const config = {
  baseUrl: env('BASE_URL', 'http://localhost:8080'),
  // Registration and sign-in are rate limited at the gateway (10/min per IP). Setup may talk to
  // identity-service directly instead, e.g. SETUP_URL=http://identity:8081 inside the network.
  setupUrl: env('SETUP_URL', env('BASE_URL', 'http://localhost:8080')),
  members: Number(env('MEMBERS', 200)),
  password: env('MEMBER_PASSWORD', 'load-test-member-password'),
  initialConnections: Number(env('INITIAL_CONNECTIONS', 5)),
  processes: Number(env('PROCESSES', 2)),
  connections: Number(env('CONNECTIONS', 64)),
  duration: Number(env('DURATION', 30)),
  warmup: Number(env('WARMUP', 5)),
  rate: env('RATE') ? Number(env('RATE')) : undefined,
  dataDir: env('DATA_DIR', path.resolve(here, '../../data/stackoverflow')),
  label: env('LABEL', 'run'),
};

const newHistogram = () => hdr.build({ lowestDiscernibleValue: 1, highestTrackableValue: 120_000_000, numberOfSignificantValueDigits: 3 });

async function api(base, method, route, { token, body } = {}) {
  const res = await fetch(base + route, {
    method,
    headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) },
    body: body ? JSON.stringify(body) : undefined,
  });
  const text = await res.text();
  let json = null;
  try { json = text ? JSON.parse(text) : null; } catch { /* non-JSON body */ }
  return { status: res.status, json, text };
}

async function withRetry(fn, attempts = 30) {
  for (let i = 0; ; i++) {
    try {
      return await fn();
    } catch (error) {
      if (i + 1 >= attempts) throw error;
      await new Promise((r) => setTimeout(r, 250 * Math.min(i + 1, 8)));
    }
  }
}

// ---------------------------------------------------------------- setup (parent process)

async function signIn(taxonomy, index) {
  const handle = `loadtest_${String(index).padStart(4, '0')}`;
  const branch = taxonomy.branches[index % taxonomy.branches.length];
  const category = taxonomy.branch_categories?.[branch] ?? taxonomy.categories[index % taxonomy.categories.length];
  return withRetry(async () => {
    let r = await api(config.setupUrl, 'POST', '/api/v1/auth/register', {
      body: { handle, name: `Load Test ${index}`, password: config.password, category, branch },
    });
    if (r.status === 409) r = await api(config.setupUrl, 'POST', '/api/v1/auth/login', { body: { handle, password: config.password } });
    if (r.status !== 200 && r.status !== 201) throw new Error(`sign-in ${handle}: HTTP ${r.status} ${r.text.slice(0, 200)}`);
    return { id: r.json.user.id, handle, token: r.json.token };
  });
}

async function setup(samplers) {
  const taxonomy = (await withRetry(async () => {
    const r = await api(config.baseUrl, 'GET', '/api/v1/meta/taxonomy');
    if (r.status !== 200) throw new Error(`taxonomy: HTTP ${r.status}`);
    return r;
  })).json;

  const members = [];
  for (let i = 1; i <= config.members; i += 8) {
    const batch = [];
    for (let j = i; j < i + 8 && j <= config.members; j++) batch.push(signIn(taxonomy, j));
    members.push(...await Promise.all(batch));
  }
  // Profiles are provisioned asynchronously from identity's user.registered event.
  await Promise.all(members.map((m) => withRetry(async () => {
    const r = await api(config.baseUrl, 'GET', '/api/v1/me/profile', { token: m.token });
    if (r.status !== 200) throw new Error(`profile of ${m.handle} not provisioned yet (HTTP ${r.status})`);
  }, 120)));

  for (const m of members) {
    const existing = await api(config.baseUrl, 'GET', '/api/v1/me/connections', { token: m.token });
    m.connections = (existing.json?.items ?? []).map((c) => c.user.id);
    while (m.connections.length < config.initialConnections) {
      const target = samplers.userId();
      const r = await api(config.baseUrl, 'POST', '/api/v1/me/connections', { token: m.token, body: { user_id: target } });
      if (r.status === 201 || r.status === 409) m.connections.push(target);
      else throw new Error(`connect ${m.handle} -> ${target}: HTTP ${r.status} ${r.text.slice(0, 200)}`);
    }
  }
  return members;
}

// ---------------------------------------------------------------- load (worker processes)

async function runWorker({ members, connections, duration, rate, warmup }) {
  const samplers = buildSamplers(loadSamples(config.dataDir));
  for (const m of members) m.connections = new Set(m.connections);
  const stats = new Map(OPERATIONS.map((op) => [op.name, { histogram: newHistogram(), statuses: {}, errors: 0 }]));
  const overall = newHistogram();
  let recording = warmup === 0;

  const request = {
    setupRequest(req, ctx) {
      for (;;) {
        const member = members[Math.floor(Math.random() * members.length)];
        const op = pickOperation();
        const built = op.build(member, samplers);
        if (!built) continue;
        ctx.op = op; ctx.member = member; ctx.built = built; ctx.start = process.hrtime.bigint();
        return {
          ...req,
          method: built.method ?? 'GET',
          path: built.path,
          headers: { authorization: `Bearer ${member.token}`, 'content-type': 'application/json' },
          body: built.body ? JSON.stringify(built.body) : undefined,
        };
      }
    },
    onResponse(status, body, ctx) {
      const micros = Number((process.hrtime.bigint() - ctx.start) / 1000n);
      const { op, member, built } = ctx;
      if (op.name === 'connect' && (status === 201 || status === 409)) member.connections.add(built.body.user_id);
      if (op.name === 'disconnect' && (status === 200 || status === 404)) member.connections.delete(built.target);
      if (!recording) return;
      const s = stats.get(op.name);
      s.histogram.recordValue(Math.max(micros, 1));
      overall.recordValue(Math.max(micros, 1));
      s.statuses[status] = (s.statuses[status] ?? 0) + 1;
      if (!op.ok.includes(status)) s.errors++;
    },
  };

  const instance = autocannon({
    url: config.baseUrl,
    connections,
    duration: duration + warmup,
    overallRate: rate,
    requests: [request],
    timeout: 10,
  });
  const warmupTimer = warmup > 0 ? setTimeout(() => { recording = true; }, warmup * 1000) : null;
  const result = await instance;
  clearTimeout(warmupTimer);
  return {
    socketErrors: result.errors,
    timeouts: result.timeouts,
    overall: hdr.encodeIntoCompressedBase64(overall),
    operations: [...stats].map(([name, s]) => ({ name, histogram: hdr.encodeIntoCompressedBase64(s.histogram), statuses: s.statuses, errors: s.errors })),
  };
}

// ---------------------------------------------------------------- report

const ms = (micros) => (micros / 1000).toFixed(micros < 10_000 ? 2 : 1);

function summarize(histogram, seconds) {
  return {
    requests: histogram.totalCount,
    rps: histogram.totalCount / seconds,
    p50_ms: Number(ms(histogram.getValueAtPercentile(50))),
    p90_ms: Number(ms(histogram.getValueAtPercentile(90))),
    p99_ms: Number(ms(histogram.getValueAtPercentile(99))),
    max_ms: Number(ms(histogram.maxValue)),
  };
}

function report(parts, seconds) {
  const overall = newHistogram();
  const ops = new Map(OPERATIONS.map((op) => [op.name, { histogram: newHistogram(), statuses: {}, errors: 0 }]));
  let socketErrors = 0;
  let timeouts = 0;
  for (const part of parts) {
    overall.add(hdr.decodeFromCompressedBase64(part.overall));
    socketErrors += part.socketErrors;
    timeouts += part.timeouts;
    for (const op of part.operations) {
      const target = ops.get(op.name);
      target.histogram.add(hdr.decodeFromCompressedBase64(op.histogram));
      target.errors += op.errors;
      for (const [status, n] of Object.entries(op.statuses)) target.statuses[status] = (target.statuses[status] ?? 0) + n;
    }
  }
  const httpErrors = [...ops.values()].reduce((sum, o) => sum + o.errors, 0);
  const total = summarize(overall, seconds);
  const operations = OPERATIONS.map((op) => {
    const o = ops.get(op.name);
    return { name: op.name, share_pct: op.weight, ...summarize(o.histogram, seconds), errors: o.errors, statuses: o.statuses };
  });
  return {
    label: config.label,
    at: new Date().toISOString(),
    config: { base_url: config.baseUrl, members: config.members, processes: config.processes, connections_per_process: config.connections, duration_s: config.duration, warmup_s: config.warmup, rate: config.rate ?? 'max' },
    machine: { cpus: os.cpus().length, model: os.cpus()[0]?.model, memory_gb: Math.round(os.totalmem() / 2 ** 30) },
    total: { ...total, http_errors: httpErrors, socket_errors: socketErrors, timeouts, error_rate_pct: total.requests ? (100 * (httpErrors + socketErrors + timeouts)) / total.requests : 0 },
    operations,
  };
}

function markdown(r) {
  const lines = [
    `## ${r.label}: ${r.at}`,
    '',
    `${r.config.processes}×${r.config.connections_per_process} connections, ${r.config.members} members, ${r.config.duration_s}s (+${r.config.warmup_s}s warm-up), rate ${r.config.rate}, ${r.machine.cpus} CPUs (${r.machine.model})`,
    '',
    `**${Math.round(r.total.rps).toLocaleString('en-US')} req/s** · p50 ${r.total.p50_ms} ms · p90 ${r.total.p90_ms} ms · p99 ${r.total.p99_ms} ms · errors ${r.total.error_rate_pct.toFixed(3)}% (${r.total.http_errors} HTTP, ${r.total.socket_errors} socket, ${r.total.timeouts} timeouts)`,
    '',
    '| Operation | Share | req/s | p50 ms | p90 ms | p99 ms | Errors |',
    '|---|---:|---:|---:|---:|---:|---:|',
    ...r.operations.map((o) => `| ${o.name} | ${o.share_pct}% | ${Math.round(o.rps).toLocaleString('en-US')} | ${o.p50_ms} | ${o.p90_ms} | ${o.p99_ms} | ${o.errors} |`),
    '',
  ];
  return lines.join('\n');
}

// ---------------------------------------------------------------- main

if (process.env.LOADTEST_WORKER === '1') {
  process.on('message', async (job) => {
    process.send(await runWorker(job));
    process.exit(0);
  });
} else {
  const samplers = buildSamplers(loadSamples(config.dataDir));
  console.error(`setting up ${config.members} members against ${config.setupUrl} ...`);
  const members = await setup(samplers);
  console.error(`running ${config.processes}×${config.connections} connections for ${config.duration}s (+${config.warmup}s warm-up) against ${config.baseUrl} ...`);
  const parts = await Promise.all(Array.from({ length: config.processes }, (_, i) => new Promise((resolve, reject) => {
    const child = fork(fileURLToPath(import.meta.url), { env: { ...process.env, LOADTEST_WORKER: '1' } });
    child.on('message', resolve);
    child.on('error', reject);
    child.on('exit', (code) => { if (code !== 0) reject(new Error(`worker ${i} exited with ${code}`)); });
    child.send({
      members: members.filter((_, k) => k % config.processes === i), // disjoint members per process
      connections: config.connections,
      duration: config.duration,
      warmup: config.warmup,
      rate: config.rate ? config.rate / config.processes : undefined,
    });
  })));
  const result = report(parts, config.duration);
  const md = markdown(result);
  console.log(md);
  const dir = path.join(here, 'results');
  mkdirSync(dir, { recursive: true });
  const stem = `${result.at.replace(/[:.]/g, '-')}-${config.label}`;
  writeFileSync(path.join(dir, `${stem}.json`), JSON.stringify(result, null, 2));
  writeFileSync(path.join(dir, `${stem}.md`), md);
}
