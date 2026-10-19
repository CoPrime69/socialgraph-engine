import { describe, expect, it } from 'vitest';
import type { GraphEdge, GraphNode } from '../../api/types';
import { topGraph } from '../../test/fixtures';
import { summarizeLegend } from './legend';
import {
  backbone,
  BACKBONE_THRESHOLD,
  buildGraphModel,
  radiusRange,
  strongestPerNode,
} from './model';
import {
  assignCommunitySlots,
  communityLabel,
  communitySizesFromNodes,
  SERIES_SLOTS,
  slotCssVar,
} from './palette';

function node(id: number, community: number, pagerank: number): GraphNode {
  return {
    id,
    handle: `user_${String(id)}`,
    name: `User ${String(id)}`,
    category: 'Web Development',
    branch: 'javascript',
    reputation: id,
    community,
    pagerank,
    degree: 1,
  };
}

describe('community palette', () => {
  it('gives the eight largest communities a colour in a fixed order and folds the rest', () => {
    const sizes = Array.from({ length: 10 }, (_, i) => ({ id: i, size: 100 - i * 5 }));
    sizes.push({ id: 99, size: 100 }); // ties are broken by id
    const slots = assignCommunitySlots(sizes);
    expect(slots.get(0)).toBe(1);
    expect(slots.get(99)).toBe(2);
    expect(slots.get(6)).toBe(8);
    expect(slots.get(7)).toBe(0);
    expect(slots.get(9)).toBe(0);
    expect(new Set(slots.values()).size).toBe(SERIES_SLOTS + 1);
  });

  it('keeps a community colour when the visible subset changes', () => {
    const all = assignCommunitySlots([
      { id: 3, size: 4210 },
      { id: 0, size: 3115 },
      { id: 7, size: 1904 },
    ]);
    const fromVisibleNodes = assignCommunitySlots(
      communitySizesFromNodes([{ community: 7 }, { community: 7 }, { community: 0 }]),
    );
    expect(all.get(7)).toBe(3);
    expect(fromVisibleNodes.get(7)).toBe(1); // the fallback ranks by what is visible
  });

  it('maps slots to tokens and names communities', () => {
    expect(slotCssVar(3)).toBe('var(--series-3)');
    expect(slotCssVar(0)).toBe('var(--series-other)');
    expect(slotCssVar(9)).toBe('var(--series-other)');
    expect(communityLabel(12)).toBe('Community 12');
  });
});

describe('legend summary', () => {
  it('lists coloured communities by slot and counts the rest as other', () => {
    const nodes = [
      node(1, 5, 0.1),
      node(2, 5, 0.1),
      node(3, 2, 0.1),
      node(4, 40, 0.1),
      node(5, 41, 0.1),
    ];
    const slots = new Map([
      [2, 1],
      [5, 2],
      [40, 0],
    ]);
    expect(summarizeLegend(nodes, slots)).toEqual({
      entries: [
        { id: 2, slot: 1, count: 1 },
        { id: 5, slot: 2, count: 2 },
      ],
      otherCommunities: 2,
      otherPeople: 2,
    });
  });
});

describe('buildGraphModel', () => {
  it('indexes nodes, sizes them by PageRank and keeps every edge for small graphs', () => {
    const data = topGraph();
    const slots = new Map([[3, 1]]);
    const model = buildGraphModel(data.nodes, data.edges, slots);
    expect(model.nodes).toHaveLength(8);
    expect(model.indexById.get(22656)).toBe(0);
    const [minR, maxR] = radiusRange(8);
    expect(model.nodes[0]?.radius).toBeCloseTo(maxR);
    expect(model.nodes.every((n) => n.radius >= minR && n.radius <= maxR)).toBe(true);
    expect(model.nodes[0]?.slot).toBe(1);
    expect(model.nodes[3]?.slot).toBe(0);
    expect(model.allStrengths).toHaveLength(5);
    expect(model.backboneEdges).toBe(model.allEdges);
    expect(model.perNode).toBe(Number.POSITIVE_INFINITY);
  });

  it('drops edges to unknown nodes and self-loops', () => {
    const nodes = [node(1, 0, 0.5), node(2, 0, 0)];
    const edges: GraphEdge[] = [
      { source: 1, target: 2, strength: 0.5 },
      { source: 1, target: 1, strength: 1 },
      { source: 1, target: 999, strength: 1 },
    ];
    const model = buildGraphModel(nodes, edges, new Map());
    expect(Array.from(model.allEdges)).toEqual([0, 1]);
  });

  it('uses the minimum radius when no one has PageRank', () => {
    const model = buildGraphModel([node(1, 0, 0), node(2, 0, 0)], [], new Map());
    expect(model.maxPagerank).toBe(0);
    expect(model.nodes[0]?.radius).toBe(radiusRange(2)[0]);
  });

  it('reduces dense graphs to a backbone of each node’s strongest edges', () => {
    const count = 120;
    const nodes = Array.from({ length: count }, (_, i) => node(i + 1, i % 3, 1 / (i + 1)));
    const edges: GraphEdge[] = [];
    for (let a = 1; a <= count; a++) {
      for (let b = a + 1; b <= count; b++)
        edges.push({ source: a, target: b, strength: ((a * b) % 97) / 97 });
    }
    expect(edges.length).toBeGreaterThan(BACKBONE_THRESHOLD);
    const model = buildGraphModel(nodes, edges, new Map());
    expect(model.perNode).toBe(strongestPerNode(count));
    expect(model.backboneStrengths.length).toBeLessThan(model.allStrengths.length);
    expect(model.backboneStrengths.length).toBeLessThanOrEqual(count * model.perNode);
    // Every node keeps at least one connection.
    const touched = new Set(model.backboneEdges);
    expect(touched.size).toBe(count);
  });

  it('keeps the strongest incident edges per node', () => {
    // Node 0 has edges of strength 0.1, 0.9 and 0.5; with perNode = 1 each node keeps its strongest.
    const edges = Uint32Array.from([0, 1, 0, 2, 0, 3]);
    const strengths = Float32Array.from([0.1, 0.9, 0.5]);
    const reduced = backbone(4, edges, strengths, 1);
    expect(Array.from(reduced.edges)).toEqual([0, 1, 0, 2, 0, 3]); // leaves 1 and 3 keep theirs
    const pruned = backbone(
      3,
      Uint32Array.from([0, 1, 0, 2, 1, 2]),
      Float32Array.from([0.9, 0.1, 0.8]),
      1,
    );
    expect(Array.from(pruned.strengths)).toEqual([expect.closeTo(0.9), expect.closeTo(0.8)]);
  });

  it('scales radii and backbone density with the number of people', () => {
    expect(radiusRange(50)).toEqual([6, 18]);
    expect(radiusRange(300)).toEqual([4, 14]);
    expect(radiusRange(1500)).toEqual([3, 12]);
    expect(radiusRange(3000)).toEqual([2.5, 10]);
    expect([200, 800, 3000].map(strongestPerNode)).toEqual([10, 6, 4]);
  });
});
