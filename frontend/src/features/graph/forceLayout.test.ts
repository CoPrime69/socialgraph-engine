import { describe, expect, it } from 'vitest';
import { topGraph } from '../../test/fixtures';
import { communityAnchors, ForceLayout, seededRandom, type LayoutRequest } from './forceLayout';
import { buildGraphModel } from './model';

function request(overrides: Partial<LayoutRequest> = {}): LayoutRequest {
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
    ...overrides,
  };
}

function run(layout: ForceLayout): Float32Array {
  while (!layout.step(1000)) {
    // keep stepping until settled
  }
  return layout.positions();
}

describe('seededRandom', () => {
  it('is deterministic per seed and stays in [0, 1)', () => {
    const a = seededRandom(7);
    const b = seededRandom(7);
    const values = Array.from({ length: 100 }, () => a());
    expect(values).toEqual(Array.from({ length: 100 }, () => b()));
    expect(values.every((v) => v >= 0 && v < 1)).toBe(true);
    expect(seededRandom(8)()).not.toBe(values[0]);
  });
});

describe('communityAnchors', () => {
  it('puts the largest community at the centre', () => {
    const anchors = communityAnchors(Int32Array.from([4, 4, 4, 1, 2]), 100);
    expect(anchors.get(4)).toEqual({ x: 0, y: 0 });
    expect(Math.hypot(anchors.get(1)?.x ?? 0, anchors.get(1)?.y ?? 0)).toBeGreaterThan(0);
  });
});

describe('ForceLayout', () => {
  it('produces the same picture for the same data', () => {
    const first = run(new ForceLayout(request()));
    const second = run(new ForceLayout(request()));
    expect(Array.from(first)).toEqual(Array.from(second));
    expect(first.every(Number.isFinite)).toBe(true);
  });

  it('reports progress and finishes', () => {
    const layout = new ForceLayout(request());
    expect(layout.progress).toBe(0);
    layout.step(1000, 10);
    expect(layout.progress).toBeCloseTo(10 / layout.maxTicks);
    run(layout);
    expect(layout.done).toBe(true);
    expect(layout.progress).toBe(1);
  });

  it('pins the ego-network centre at the origin', () => {
    const positions = run(new ForceLayout(request({ pinned: 2 })));
    expect(positions[4]).toBe(0);
    expect(positions[5]).toBe(0);
  });

  it('warm-starts from known positions and moves them only a little', () => {
    const settled = run(new ForceLayout(request()));
    const warm = new ForceLayout(request({ initial: settled }));
    expect(warm.progress).toBeGreaterThan(0.5);
    const again = run(warm);
    let moved = 0;
    for (let i = 0; i < settled.length; i++)
      moved = Math.max(moved, Math.abs((again[i] ?? 0) - (settled[i] ?? 0)));
    expect(moved).toBeLessThan(40);
  });
});
