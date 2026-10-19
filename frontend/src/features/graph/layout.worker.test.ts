import { beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import { topGraph } from '../../test/fixtures';
import type { LayoutRequest } from './forceLayout';
import type { LayoutWorkerRequest, LayoutWorkerResponse } from './layoutProtocol';
import { buildGraphModel } from './model';

/**
 * The worker module talks to `self`; here `self` is the test window, so the module's message
 * handler is called directly and `postMessage` is replaced by a recorder.
 */
type Handler = (event: MessageEvent<LayoutWorkerRequest>) => void;
const posted: LayoutWorkerResponse[] = [];
let handler: Handler;

function send(message: LayoutWorkerRequest): void {
  handler({ data: message } as MessageEvent<LayoutWorkerRequest>);
}

function layoutRequest(): LayoutRequest {
  const data = topGraph();
  const model = buildGraphModel(data.nodes, data.edges, new Map());
  const n = model.nodes.length;
  return {
    nodeCount: n,
    radius: Float32Array.from(model.nodes, (node) => node.radius),
    community: Int32Array.from(model.nodes, (node) => node.community),
    edges: model.allEdges,
    strengths: model.allStrengths,
    initial: new Float32Array(n * 2).fill(Number.NaN),
    pinned: -1,
    seed: 20081,
  };
}

/** Enough people that a layout takes several time slices. */
function largeRequest(count = 2000): LayoutRequest {
  return {
    nodeCount: count,
    radius: new Float32Array(count).fill(3),
    community: Int32Array.from({ length: count }, (_, i) => i % 7),
    edges: new Uint32Array(0),
    strengths: new Float32Array(0),
    initial: new Float32Array(count * 2).fill(Number.NaN),
    pinned: -1,
    seed: 1,
  };
}

beforeAll(async () => {
  await import('./layout.worker');
  handler = (self as unknown as { onmessage: Handler }).onmessage;
});

beforeEach(() => {
  posted.length = 0;
  vi.spyOn(window, 'postMessage').mockImplementation(((message: LayoutWorkerResponse) => {
    posted.push(message);
  }) as typeof window.postMessage);
});

describe('layout worker', () => {
  it('posts the final positions for the latest run', async () => {
    send({ type: 'start', runId: 1, request: layoutRequest(), animate: true });
    await vi.waitFor(() => {
      expect(posted.some((message) => message.type === 'positions' && message.done)).toBe(true);
    });
    const final = posted.find((message) => message.type === 'positions' && message.done);
    expect(final).toMatchObject({ runId: 1, progress: 1 });
  });

  it('reports progress without positions in reduced-motion mode', async () => {
    send({ type: 'start', runId: 2, request: layoutRequest(), animate: false });
    await vi.waitFor(() => {
      expect(posted.at(-1)).toMatchObject({ type: 'positions', runId: 2, done: true });
    });
    expect(posted.filter((message) => message.type === 'positions')).toHaveLength(1);
  });

  it('abandons a run when stopped', async () => {
    send({ type: 'start', runId: 3, request: largeRequest(), animate: true });
    send({ type: 'stop' });
    const before = posted.length;
    await new Promise((resolve) => setTimeout(resolve, 60));
    expect(posted.length).toBe(before);
    expect(posted.some((message) => message.type === 'positions' && message.done)).toBe(false);
  });

  it('lets a newer run supersede a running one', async () => {
    send({ type: 'start', runId: 4, request: largeRequest(), animate: false });
    send({ type: 'start', runId: 5, request: layoutRequest(), animate: false });
    await vi.waitFor(() => {
      expect(posted.at(-1)).toMatchObject({ type: 'positions', runId: 5, done: true });
    });
    await new Promise((resolve) => setTimeout(resolve, 60));
    expect(posted.filter((message) => message.runId === 4 && message.type === 'positions')).toEqual(
      [],
    );
  });
});
