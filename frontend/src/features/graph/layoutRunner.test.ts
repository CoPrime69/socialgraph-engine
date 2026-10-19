import { afterEach, describe, expect, it, vi } from 'vitest';
import { topGraph } from '../../test/fixtures';
import type { LayoutRequest } from './forceLayout';
import type { LayoutWorkerRequest, LayoutWorkerResponse } from './layoutProtocol';
import { createLayoutRunner } from './layoutRunner';
import { buildGraphModel } from './model';

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
    pinned: 0,
    seed: 20081,
  };
}

describe('inline layout runner (no Web Worker available)', () => {
  it('streams positions while animating and finishes with the final layout', async () => {
    const runner = createLayoutRunner();
    const onPositions = vi.fn();
    runner.start(layoutRequest(), true, { onPositions });
    await vi.waitFor(() => {
      expect(onPositions).toHaveBeenLastCalledWith(expect.any(Float32Array), true);
    });
    const [positions] = onPositions.mock.lastCall as [Float32Array, boolean];
    expect(positions).toHaveLength(16);
    runner.dispose();
  });

  it('reports only progress and the final layout when motion is reduced', async () => {
    const runner = createLayoutRunner();
    const onPositions = vi.fn();
    const onProgress = vi.fn();
    runner.start(layoutRequest(), false, { onPositions, onProgress });
    await vi.waitFor(() => {
      expect(onPositions).toHaveBeenCalledTimes(1);
    });
    expect(onPositions).toHaveBeenCalledWith(expect.any(Float32Array), true);
    runner.dispose();
  });

  it('cancels a run that is stopped', async () => {
    const runner = createLayoutRunner();
    const onPositions = vi.fn();
    runner.start(layoutRequest(), true, { onPositions });
    runner.stop();
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(onPositions).not.toHaveBeenCalled();
  });
});

describe('Web Worker layout runner', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('runs the layout in a worker and ignores results from superseded runs', () => {
    const workers: FakeWorker[] = [];
    class FakeWorker {
      onmessage: ((event: MessageEvent<LayoutWorkerResponse>) => void) | null = null;
      posted: LayoutWorkerRequest[] = [];
      terminated = false;
      constructor() {
        workers.push(this);
      }
      postMessage(message: LayoutWorkerRequest) {
        this.posted.push(message);
      }
      terminate() {
        this.terminated = true;
      }
      reply(message: LayoutWorkerResponse) {
        this.onmessage?.({ data: message } as MessageEvent<LayoutWorkerResponse>);
      }
    }
    vi.stubGlobal('Worker', FakeWorker);

    const runner = createLayoutRunner();
    const worker = workers[0];
    if (!worker) throw new Error('no worker created');
    const onPositions = vi.fn();
    const onProgress = vi.fn();
    runner.start(layoutRequest(), false, { onPositions, onProgress });
    runner.start(layoutRequest(), false, { onPositions, onProgress });
    expect(worker.posted.map((message) => message.type)).toEqual(['start', 'start']);

    const positions = new Float32Array(16);
    worker.reply({ type: 'positions', runId: 1, positions, progress: 1, done: true });
    expect(onPositions).not.toHaveBeenCalled();
    worker.reply({ type: 'progress', runId: 2, progress: 0.5 });
    worker.reply({ type: 'positions', runId: 2, positions, progress: 1, done: true });
    expect(onProgress).toHaveBeenCalledWith(0.5);
    expect(onPositions).toHaveBeenCalledWith(positions, true);

    runner.stop();
    expect(worker.posted.at(-1)).toEqual({ type: 'stop' });
    worker.reply({ type: 'positions', runId: 2, positions, progress: 1, done: true });
    expect(onPositions).toHaveBeenCalledTimes(1);

    runner.dispose();
    expect(worker.terminated).toBe(true);
  });

  it('falls back to the main thread when the worker cannot start', async () => {
    vi.stubGlobal('Worker', function BlockedWorker() {
      throw new Error('blocked by policy');
    });
    const runner = createLayoutRunner();
    const onPositions = vi.fn();
    runner.start(layoutRequest(), false, { onPositions });
    await vi.waitFor(() => {
      expect(onPositions).toHaveBeenCalledWith(expect.any(Float32Array), true);
    });
    runner.dispose();
  });
});
