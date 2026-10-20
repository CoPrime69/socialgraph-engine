import type { GraphEdge, GraphNode } from '../../api/types';
import type { PaletteSlot } from './palette';

/**
 * Render model derived from a `GET /analytics/graph` response: nodes get a dense index,
 * a radius (√PageRank within the visible set) and a palette slot; edges are reduced to a
 * readable "backbone" for dense subgraphs.
 */

export interface ModelNode extends GraphNode {
  index: number;
  radius: number;
  slot: PaletteSlot;
}

export interface GraphModel {
  nodes: ModelNode[];
  indexById: Map<number, number>;
  /** All edges between returned nodes, as index pairs. */
  allEdges: Uint32Array;
  allStrengths: Float32Array;
  /** Backbone edges (each node's strongest connections), as index pairs. */
  backboneEdges: Uint32Array;
  backboneStrengths: Float32Array;
  /** Strongest-edges-per-node used for the backbone (Infinity when every edge is kept). */
  perNode: number;
  maxPagerank: number;
}

/** Below this many edges every edge is drawn and used for layout. */
export const BACKBONE_THRESHOLD = 4000;

export function strongestPerNode(nodeCount: number): number {
  if (nodeCount <= 300) return 10;
  if (nodeCount <= 1000) return 6;
  return 4;
}

export function radiusRange(nodeCount: number): [number, number] {
  if (nodeCount <= 60) return [6, 18];
  if (nodeCount <= 400) return [4, 14];
  if (nodeCount <= 1500) return [3, 12];
  return [2.5, 10];
}

/**
 * Keeps, for every node, its `perNode` strongest incident edges (an edge survives if either
 * endpoint keeps it). Ties are broken by the other endpoint's index for determinism.
 */
export function backbone(
  nodeCount: number,
  edges: Uint32Array,
  strengths: Float32Array,
  perNode: number,
): { edges: Uint32Array; strengths: Float32Array } {
  const edgeCount = strengths.length;
  const incident: number[][] = Array.from({ length: nodeCount }, () => []);
  for (let e = 0; e < edgeCount; e++) {
    incident[edges[2 * e] ?? 0]?.push(e);
    incident[edges[2 * e + 1] ?? 0]?.push(e);
  }
  const keep = new Uint8Array(edgeCount);
  for (const list of incident) {
    list.sort((a, b) => (strengths[b] ?? 0) - (strengths[a] ?? 0) || a - b);
    for (let i = 0; i < Math.min(perNode, list.length); i++) keep[list[i] ?? 0] = 1;
  }
  let kept = 0;
  for (let e = 0; e < edgeCount; e++) kept += keep[e] ?? 0;
  const outEdges = new Uint32Array(kept * 2);
  const outStrengths = new Float32Array(kept);
  let k = 0;
  for (let e = 0; e < edgeCount; e++) {
    if (!keep[e]) continue;
    outEdges[2 * k] = edges[2 * e] ?? 0;
    outEdges[2 * k + 1] = edges[2 * e + 1] ?? 0;
    outStrengths[k] = strengths[e] ?? 0;
    k++;
  }
  return { edges: outEdges, strengths: outStrengths };
}

export function buildGraphModel(
  nodes: readonly GraphNode[],
  edges: readonly GraphEdge[],
  slots: ReadonlyMap<number, PaletteSlot>,
): GraphModel {
  const indexById = new Map<number, number>();
  nodes.forEach((node, index) => indexById.set(node.id, index));

  let maxPagerank = 0;
  for (const node of nodes) maxPagerank = Math.max(maxPagerank, node.pagerank);
  const [minR, maxR] = radiusRange(nodes.length);
  const modelNodes: ModelNode[] = nodes.map((node, index) => ({
    ...node,
    index,
    radius:
      maxPagerank > 0
        ? minR + (maxR - minR) * Math.sqrt(Math.max(0, node.pagerank) / maxPagerank)
        : minR,
    slot: slots.get(node.community) ?? 0,
  }));

  const pairs: number[] = [];
  const strengthList: number[] = [];
  for (const edge of edges) {
    const a = indexById.get(edge.source);
    const b = indexById.get(edge.target);
    if (a === undefined || b === undefined || a === b) continue;
    pairs.push(a, b);
    strengthList.push(edge.strength);
  }
  const allEdges = Uint32Array.from(pairs);
  const allStrengths = Float32Array.from(strengthList);

  if (strengthList.length <= BACKBONE_THRESHOLD) {
    return {
      nodes: modelNodes,
      indexById,
      allEdges,
      allStrengths,
      backboneEdges: allEdges,
      backboneStrengths: allStrengths,
      perNode: Number.POSITIVE_INFINITY,
      maxPagerank,
    };
  }
  const perNode = strongestPerNode(nodes.length);
  const reduced = backbone(nodes.length, allEdges, allStrengths, perNode);
  return {
    nodes: modelNodes,
    indexById,
    allEdges,
    allStrengths,
    backboneEdges: reduced.edges,
    backboneStrengths: reduced.strengths,
    perNode,
    maxPagerank,
  };
}
