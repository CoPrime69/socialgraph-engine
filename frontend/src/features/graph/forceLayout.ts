import {
  forceCollide,
  forceLink,
  forceManyBody,
  forceSimulation,
  forceX,
  forceY,
  type Simulation,
  type SimulationLinkDatum,
  type SimulationNodeDatum,
} from 'd3-force';

/**
 * Deterministic d3-force layout used both inside the Web Worker and (as a fallback) on the
 * main thread. Nodes start near an anchor per community, which converges quickly and makes
 * community structure readable; a weak pull towards the anchors keeps clusters apart.
 */

export interface LayoutRequest {
  nodeCount: number;
  radius: Float32Array;
  community: Int32Array;
  /** Index pairs (a0, b0, a1, b1, …). */
  edges: Uint32Array;
  strengths: Float32Array;
  /** Known positions from a previous layout (x, y pairs); NaN where unknown. */
  initial: Float32Array;
  /** Node pinned at the origin (ego-network centre), or -1. */
  pinned: number;
  seed: number;
}

interface SimNode extends SimulationNodeDatum {
  index: number;
  r: number;
  ax: number;
  ay: number;
}

type SimLink = SimulationLinkDatum<SimNode> & { strength: number };

/** Small, fast, seedable PRNG (mulberry32). */
export function seededRandom(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let t = state;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const GOLDEN_ANGLE = Math.PI * (3 - Math.sqrt(5));

/** Anchor point per community: largest communities nearest the centre, on a sunflower spiral. */
export function communityAnchors(
  community: Int32Array,
  spread: number,
): Map<number, { x: number; y: number }> {
  const counts = new Map<number, number>();
  for (const c of community) counts.set(c, (counts.get(c) ?? 0) + 1);
  const ranked = Array.from(counts).sort((a, b) => b[1] - a[1] || a[0] - b[0]);
  const anchors = new Map<number, { x: number; y: number }>();
  ranked.forEach(([id], i) => {
    const distance = i === 0 ? 0 : spread * Math.sqrt(i / Math.max(1, ranked.length - 1));
    anchors.set(id, {
      x: distance * Math.cos(i * GOLDEN_ANGLE),
      y: distance * Math.sin(i * GOLDEN_ANGLE),
    });
  });
  return anchors;
}

export class ForceLayout {
  private readonly simulation: Simulation<SimNode, SimLink>;
  private readonly nodes: SimNode[];
  private ticks = 0;
  readonly maxTicks: number;

  constructor(request: LayoutRequest) {
    const n = request.nodeCount;
    const random = seededRandom(request.seed);
    const spread = 14 * Math.sqrt(Math.max(1, n));
    const anchors = communityAnchors(
      request.community,
      request.pinned >= 0 ? spread * 0.25 : spread,
    );
    let known = 0;

    this.nodes = Array.from({ length: n }, (_, index): SimNode => {
      const anchor = anchors.get(request.community[index] ?? 0) ?? { x: 0, y: 0 };
      const px = request.initial[2 * index];
      const py = request.initial[2 * index + 1];
      const hasPrevious =
        px !== undefined && py !== undefined && Number.isFinite(px) && Number.isFinite(py);
      if (hasPrevious) known++;
      const jitter = spread * 0.18;
      return {
        index,
        r: request.radius[index] ?? 3,
        ax: anchor.x,
        ay: anchor.y,
        x: hasPrevious ? px : anchor.x + (random() - 0.5) * jitter,
        y: hasPrevious ? py : anchor.y + (random() - 0.5) * jitter,
      };
    });

    const pinned = this.nodes[request.pinned];
    if (pinned) {
      pinned.fx = 0;
      pinned.fy = 0;
    }

    const links: SimLink[] = [];
    for (let e = 0; e < request.strengths.length; e++) {
      links.push({
        source: request.edges[2 * e] ?? 0,
        target: request.edges[2 * e + 1] ?? 0,
        strength: request.strengths[e] ?? 0,
      });
    }

    const large = n > 1000;
    const warmStart = known > n * 0.6;
    this.maxTicks = large ? 260 : 320;
    const anchorPull = request.pinned >= 0 ? 0.01 : 0.035;

    this.simulation = forceSimulation<SimNode, SimLink>(this.nodes)
      .randomSource(random)
      .alpha(warmStart ? 0.35 : 1)
      .alphaDecay(1 - Math.pow(0.001, 1 / this.maxTicks))
      .velocityDecay(0.42)
      .force(
        'link',
        forceLink<SimNode, SimLink>(links)
          .id((node) => node.index)
          .distance((link) => (large ? 18 : 28) + (1 - link.strength) * (large ? 14 : 22)),
      )
      .force(
        'charge',
        forceManyBody<SimNode>()
          .strength(large ? -16 : n > 300 ? -28 : -60)
          .theta(0.9)
          .distanceMax(large ? 220 : 400),
      )
      .force('x', forceX<SimNode>((node) => node.ax).strength(anchorPull))
      .force('y', forceY<SimNode>((node) => node.ay).strength(anchorPull))
      .force(
        'collide',
        forceCollide<SimNode>((node) => node.r + 1.2)
          .strength(0.7)
          .iterations(1),
      )
      .stop();

    if (warmStart) this.ticks = Math.floor(this.maxTicks * 0.65);
  }

  get done(): boolean {
    return this.ticks >= this.maxTicks || this.simulation.alpha() < this.simulation.alphaMin();
  }

  /** Fraction of the planned ticks completed, 0..1. */
  get progress(): number {
    return this.done ? 1 : Math.min(1, this.ticks / this.maxTicks);
  }

  /**
   * Advances the simulation for up to `budgetMs` (and at most `maxTicks` ticks).
   * Returns true once the layout has settled.
   */
  step(budgetMs: number, maxTicks = Number.POSITIVE_INFINITY): boolean {
    const start = performance.now();
    let ran = 0;
    while (!this.done && ran < maxTicks) {
      this.simulation.tick();
      this.ticks++;
      ran++;
      if (performance.now() - start >= budgetMs) break;
    }
    return this.done;
  }

  positions(): Float32Array {
    const out = new Float32Array(this.nodes.length * 2);
    this.nodes.forEach((node, i) => {
      out[2 * i] = node.x ?? 0;
      out[2 * i + 1] = node.y ?? 0;
    });
    return out;
  }
}
