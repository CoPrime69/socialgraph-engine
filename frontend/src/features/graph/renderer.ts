import { quadtree, type Quadtree } from 'd3-quadtree';
import { select } from 'd3-selection';
import {
  zoom,
  zoomIdentity,
  type D3ZoomEvent,
  type ZoomBehavior,
  type ZoomTransform,
} from 'd3-zoom';
import { truncate } from '../../lib/text';
import type { GraphModel, ModelNode } from './model';
import { SERIES_SLOTS } from './palette';
import type { GraphTheme } from './theme';

/**
 * Canvas renderer for the network graph: draws edges, nodes and labels in batches, handles
 * zoom/pan with d3-zoom and hit-tests the pointer with a quadtree.
 *
 * Gestures are chosen so the graph never traps page scrolling: a plain mouse wheel (and a
 * one-finger swipe) scrolls the page; Ctrl/⌘ + wheel, trackpad pinch and two-finger touch zoom;
 * dragging pans. The zoom buttons and the table view offer the same information without them.
 */

export interface GraphPoint {
  x: number;
  y: number;
}

export interface RendererCallbacks {
  /** Pointer moved onto (or off) a node; `point` is the node centre in CSS pixels. */
  onHover: (node: ModelNode | null, point: GraphPoint | null) => void;
  onSelect: (node: ModelNode) => void;
}

export interface RendererOptions {
  /** Community to emphasise (every other one is dimmed), or null. */
  highlightCommunity: number | null;
  /** The signed-in user: drawn with an accent ring and labelled "You". */
  youId: number | null;
  /** Centre of an ego network: always labelled. */
  focusId: number | null;
  /** Draw every edge instead of each node's strongest ones. */
  showAllEdges: boolean;
}

export const MIN_ZOOM = 0.05;
export const MAX_ZOOM = 12;
const MAX_FIT_ZOOM = 2.5;
const FIT_PADDING_PX = 28;
const HIT_SLOP_PX = 6;
const MAX_LABELS = 30;
/** Nodes get a name label once they are at least this large on screen. */
const LABEL_MIN_SCREEN_RADIUS = 7;
const LABEL_FONT_PX = 12;
const DIMMED_ALPHA = 0.16;
/** While zooming/panning, very dense views temporarily draw only the backbone edges. */
const INTERACTION_EDGE_BUDGET = 15_000;
const TAU = Math.PI * 2;

/** "Other" first so that the eight named communities are drawn on top of it. */
const SLOT_DRAW_ORDER: readonly number[] = [
  0,
  ...Array.from({ length: SERIES_SLOTS }, (_, i) => SERIES_SLOTS - i),
];

/** Edge opacity falls as the number of drawn edges grows, keeping dense views readable. */
export function edgeAlpha(edgeCount: number): number {
  if (edgeCount <= 500) return 0.5;
  if (edgeCount <= 2000) return 0.35;
  if (edgeCount <= 8000) return 0.22;
  return 0.12;
}

/**
 * Zoom step for a wheel event. Trackpad pinches arrive as ctrl + wheel with small deltas and
 * are amplified; mouse-wheel notches (~100 px) are not, and every step is clamped.
 */
export function wheelDelta(event: WheelEvent): number {
  const unit = event.deltaMode === 1 ? 0.05 : event.deltaMode === 2 ? 1 : 0.002;
  const pinch = event.ctrlKey && Math.abs(event.deltaY) < 40 ? 10 : 1;
  return Math.max(-0.5, Math.min(0.5, -event.deltaY * unit * pinch));
}

/** Which input events may start a zoom/pan gesture (see the module comment). */
export function zoomFilter(event: Event): boolean {
  if (event.type === 'wheel') {
    const wheel = event as WheelEvent;
    return wheel.ctrlKey || wheel.metaKey;
  }
  if (event.type === 'touchstart') {
    return (event as TouchEvent).touches.length >= 2;
  }
  const mouse = event as MouseEvent;
  return !mouse.ctrlKey && !mouse.button;
}

/** Transform that fits `bounds` (world units) into a `width`×`height` viewport. */
export function fitTransform(
  bounds: { minX: number; minY: number; maxX: number; maxY: number },
  width: number,
  height: number,
): ZoomTransform {
  const spanX = Math.max(1, bounds.maxX - bounds.minX);
  const spanY = Math.max(1, bounds.maxY - bounds.minY);
  const availableX = Math.max(1, width - 2 * FIT_PADDING_PX);
  const availableY = Math.max(1, height - 2 * FIT_PADDING_PX);
  const k = Math.max(MIN_ZOOM, Math.min(MAX_FIT_ZOOM, availableX / spanX, availableY / spanY));
  const cx = (bounds.minX + bounds.maxX) / 2;
  const cy = (bounds.minY + bounds.maxY) / 2;
  return zoomIdentity.translate(width / 2 - k * cx, height / 2 - k * cy).scale(k);
}

const DEFAULT_OPTIONS: RendererOptions = {
  highlightCommunity: null,
  youId: null,
  focusId: null,
  showAllEdges: false,
};

export class GraphRenderer {
  private readonly canvas: HTMLCanvasElement;
  private readonly context: CanvasRenderingContext2D | null;
  private readonly callbacks: RendererCallbacks;
  private readonly zoomBehavior: ZoomBehavior<HTMLCanvasElement, unknown>;
  private theme: GraphTheme;
  private transform: ZoomTransform = zoomIdentity;
  private model: GraphModel | null = null;
  private positions: Float32Array | null = null;
  private options: RendererOptions = DEFAULT_OPTIONS;
  private nodesBySlot: number[][] = [];
  /** Node indices by radius, largest first (label candidates). */
  private labelOrder: number[] = [];
  private maxRadius = 0;
  private minRadius = 0;
  private youIndex = -1;
  private focusIndex = -1;
  private hovered = -1;
  private tree: Quadtree<number> | null = null;
  private treeDirty = true;
  private width = 0;
  private height = 0;
  private dpr = 1;
  private frame: number | null = null;
  private interacting = false;
  /** Set once the user zooms or pans; until then the view keeps fitting the layout. */
  private userMovedView = false;
  private destroyed = false;

  constructor(canvas: HTMLCanvasElement, theme: GraphTheme, callbacks: RendererCallbacks) {
    this.canvas = canvas;
    this.context = canvas.getContext('2d');
    this.theme = theme;
    this.callbacks = callbacks;

    this.zoomBehavior = zoom<HTMLCanvasElement, unknown>()
      .scaleExtent([MIN_ZOOM, MAX_ZOOM])
      .extent(() => [
        [0, 0],
        [Math.max(1, this.width), Math.max(1, this.height)],
      ])
      .filter(zoomFilter)
      .wheelDelta(wheelDelta)
      .on('start', (event: D3ZoomEvent<HTMLCanvasElement, unknown>) => {
        if (!event.sourceEvent) return;
        this.interacting = true;
        this.setHovered(-1);
      })
      .on('zoom', (event: D3ZoomEvent<HTMLCanvasElement, unknown>) => {
        this.transform = event.transform;
        if (event.sourceEvent) this.userMovedView = true;
        this.requestDraw();
      })
      .on('end', () => {
        if (!this.interacting) return;
        this.interacting = false;
        this.requestDraw();
      });
    select(canvas).call(this.zoomBehavior);

    canvas.addEventListener('pointermove', this.handlePointerMove);
    canvas.addEventListener('pointerleave', this.handlePointerLeave);
    canvas.addEventListener('click', this.handleClick);
  }

  // ---------- Inputs ----------

  /**
   * Replaces the drawn graph. `keepPositions` keeps the current layout when only colours
   * changed (same nodes in the same order).
   */
  setModel(model: GraphModel, keepPositions = false): void {
    const sameShape =
      keepPositions && this.positions !== null && this.positions.length === model.nodes.length * 2;
    this.model = model;
    if (!sameShape) this.positions = null;
    this.nodesBySlot = Array.from({ length: SERIES_SLOTS + 1 }, () => []);
    let maxRadius = 0;
    let minRadius = Number.POSITIVE_INFINITY;
    for (const node of model.nodes) {
      this.nodesBySlot[node.slot]?.push(node.index);
      maxRadius = Math.max(maxRadius, node.radius);
      minRadius = Math.min(minRadius, node.radius);
    }
    this.maxRadius = maxRadius;
    this.minRadius = Number.isFinite(minRadius) ? minRadius : 0;
    this.labelOrder = model.nodes
      .map((node) => node.index)
      .sort((a, b) => (model.nodes[b]?.radius ?? 0) - (model.nodes[a]?.radius ?? 0) || a - b);
    this.hovered = -1;
    this.treeDirty = true;
    this.resolveOptionIndices();
    this.requestDraw();
  }

  /** New layout positions (x, y pairs by node index, world units). */
  setPositions(positions: Float32Array): void {
    if (!this.model || positions.length !== this.model.nodes.length * 2) return;
    this.positions = positions;
    this.treeDirty = true;
    if (!this.userMovedView) this.applyFit();
    this.requestDraw();
  }

  setOptions(options: RendererOptions): void {
    this.options = options;
    this.resolveOptionIndices();
    this.requestDraw();
  }

  setTheme(theme: GraphTheme): void {
    this.theme = theme;
    this.requestDraw();
  }

  /** Sets the CSS size of the drawing surface; the backing store is scaled for crisp lines. */
  resize(width: number, height: number, devicePixelRatio = window.devicePixelRatio || 1): void {
    this.width = Math.max(0, width);
    this.height = Math.max(0, height);
    this.dpr = Math.min(2, Math.max(1, devicePixelRatio));
    this.canvas.width = Math.max(1, Math.round(this.width * this.dpr));
    this.canvas.height = Math.max(1, Math.round(this.height * this.dpr));
    if (!this.userMovedView) this.applyFit();
    this.requestDraw();
  }

  // ---------- View controls ----------

  zoomBy(factor: number): void {
    this.userMovedView = true;
    this.zoomBehavior.scaleBy(select(this.canvas), factor, [this.width / 2, this.height / 2]);
  }

  /** Fits the whole graph and resumes following the layout. */
  fitToView(): void {
    this.userMovedView = false;
    this.applyFit();
  }

  get zoomLevel(): number {
    return this.transform.k;
  }

  // ---------- Hit testing ----------

  /** The node under a point given in CSS pixels relative to the canvas, if any. */
  nodeAtPoint(x: number, y: number): ModelNode | null {
    const model = this.model;
    const positions = this.positions;
    if (!model || !positions) return null;
    const tree = this.ensureTree(positions);
    const [wx, wy] = this.transform.invert([x, y]);
    const slop = HIT_SLOP_PX / this.transform.k;
    const found = tree.find(wx, wy, this.maxRadius + slop);
    if (found === undefined) return null;
    const node = model.nodes[found];
    if (!node) return null;
    const dx = (positions[2 * found] ?? 0) - wx;
    const dy = (positions[2 * found + 1] ?? 0) - wy;
    return Math.hypot(dx, dy) <= node.radius + slop ? node : null;
  }

  /** Screen position (CSS pixels) of a node's centre. */
  screenPosition(index: number): GraphPoint | null {
    const positions = this.positions;
    if (!positions || index < 0 || 2 * index + 1 >= positions.length) return null;
    const [x, y] = this.transform.apply([positions[2 * index] ?? 0, positions[2 * index + 1] ?? 0]);
    return { x, y };
  }

  // ---------- Lifecycle ----------

  requestDraw(): void {
    if (this.destroyed || this.frame !== null) return;
    const schedule =
      typeof requestAnimationFrame === 'function'
        ? requestAnimationFrame
        : (callback: FrameRequestCallback) => window.setTimeout(() => callback(Date.now()), 16);
    this.frame = schedule(() => {
      this.frame = null;
      this.draw();
    });
  }

  destroy(): void {
    this.destroyed = true;
    if (this.frame !== null) {
      if (typeof requestAnimationFrame === 'function') cancelAnimationFrame(this.frame);
      else window.clearTimeout(this.frame);
    }
    this.frame = null;
    select(this.canvas).on('.zoom', null);
    this.canvas.removeEventListener('pointermove', this.handlePointerMove);
    this.canvas.removeEventListener('pointerleave', this.handlePointerLeave);
    this.canvas.removeEventListener('click', this.handleClick);
  }

  // ---------- Drawing ----------

  draw(): void {
    const ctx = this.context;
    if (!ctx || this.destroyed) return;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    const model = this.model;
    const positions = this.positions;
    if (!model || !positions || this.width === 0 || this.height === 0) return;

    const { k, x, y } = this.transform;
    const dpr = this.dpr;
    ctx.setTransform(dpr * k, 0, 0, dpr * k, dpr * x, dpr * y);
    const px = 1 / k; // one CSS pixel in world units
    this.drawEdges(ctx, model, positions, px);
    this.drawNodes(ctx, model, positions, px);
    this.drawEmphasis(ctx, model, positions, px);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.drawLabels(ctx, model);
  }

  private drawnEdges(model: GraphModel): Uint32Array {
    if (!this.options.showAllEdges) return model.backboneEdges;
    const tooDense = this.interacting && model.allStrengths.length > INTERACTION_EDGE_BUDGET;
    return tooDense ? model.backboneEdges : model.allEdges;
  }

  private drawEdges(
    ctx: CanvasRenderingContext2D,
    model: GraphModel,
    positions: Float32Array,
    px: number,
  ): void {
    const edges = this.drawnEdges(model);
    const count = edges.length / 2;
    if (count === 0) return;
    const alpha = edgeAlpha(count);
    const highlight = this.options.highlightCommunity;
    const nodes = model.nodes;
    ctx.lineWidth = px;
    ctx.strokeStyle = this.theme.edge;

    const stroke = (opacity: number, include: (a: number, b: number) => boolean) => {
      ctx.globalAlpha = opacity;
      ctx.beginPath();
      for (let e = 0; e < count; e++) {
        const a = edges[2 * e] ?? 0;
        const b = edges[2 * e + 1] ?? 0;
        if (!include(a, b)) continue;
        ctx.moveTo(positions[2 * a] ?? 0, positions[2 * a + 1] ?? 0);
        ctx.lineTo(positions[2 * b] ?? 0, positions[2 * b + 1] ?? 0);
      }
      ctx.stroke();
    };

    if (highlight === null) {
      stroke(alpha, () => true);
    } else {
      const inside = (i: number) => nodes[i]?.community === highlight;
      stroke(alpha * 0.25, (a, b) => !(inside(a) && inside(b)));
      stroke(Math.min(0.6, alpha * 2), (a, b) => inside(a) && inside(b));
    }
    ctx.globalAlpha = 1;
  }

  private drawNodes(
    ctx: CanvasRenderingContext2D,
    model: GraphModel,
    positions: Float32Array,
    px: number,
  ): void {
    const highlight = this.options.highlightCommunity;
    const nodes = model.nodes;
    // A thin ring in the surface colour separates overlapping nodes; it never eats tiny nodes.
    ctx.lineWidth = Math.min(1.25 * px, Math.max(0.1, this.minRadius * 0.35));
    ctx.strokeStyle = this.theme.ring;

    const pass = (opacity: number, include: (index: number) => boolean) => {
      ctx.globalAlpha = opacity;
      for (const slot of SLOT_DRAW_ORDER) {
        const members = this.nodesBySlot[slot];
        if (!members || members.length === 0) continue;
        ctx.beginPath();
        let any = false;
        for (const index of members) {
          if (!include(index)) continue;
          const node = nodes[index];
          if (!node) continue;
          const nx = positions[2 * index] ?? 0;
          const ny = positions[2 * index + 1] ?? 0;
          ctx.moveTo(nx + node.radius, ny);
          ctx.arc(nx, ny, node.radius, 0, TAU);
          any = true;
        }
        if (!any) continue;
        ctx.fillStyle = this.theme.series[slot] ?? this.theme.series[0] ?? '#8f8d86';
        ctx.fill();
        ctx.stroke();
      }
    };

    if (highlight === null) {
      pass(1, () => true);
    } else {
      pass(DIMMED_ALPHA, (i) => nodes[i]?.community !== highlight);
      pass(1, (i) => nodes[i]?.community === highlight);
    }
    ctx.globalAlpha = 1;
  }

  private drawEmphasis(
    ctx: CanvasRenderingContext2D,
    model: GraphModel,
    positions: Float32Array,
    px: number,
  ): void {
    const hovered = this.hovered;
    if (hovered >= 0) {
      // Every connection of the hovered person, including those the backbone hides.
      const edges = model.allEdges;
      ctx.globalAlpha = 0.75;
      ctx.strokeStyle = this.theme.highlight;
      ctx.lineWidth = 1.25 * px;
      ctx.beginPath();
      for (let e = 0; e < edges.length / 2; e++) {
        const a = edges[2 * e] ?? 0;
        const b = edges[2 * e + 1] ?? 0;
        if (a !== hovered && b !== hovered) continue;
        ctx.moveTo(positions[2 * a] ?? 0, positions[2 * a + 1] ?? 0);
        ctx.lineTo(positions[2 * b] ?? 0, positions[2 * b + 1] ?? 0);
      }
      ctx.stroke();
      ctx.globalAlpha = 1;
    }

    const ring = (index: number, color: string, widthPx: number) => {
      const node = model.nodes[index];
      if (!node) return;
      const width = widthPx * px;
      ctx.beginPath();
      ctx.arc(
        positions[2 * index] ?? 0,
        positions[2 * index + 1] ?? 0,
        node.radius + width / 2 + 0.75 * px,
        0,
        TAU,
      );
      ctx.strokeStyle = color;
      ctx.lineWidth = width;
      ctx.stroke();
    };
    if (this.focusIndex >= 0 && this.focusIndex !== this.youIndex) {
      ring(this.focusIndex, this.theme.highlight, 2);
    }
    if (this.youIndex >= 0) ring(this.youIndex, this.theme.you, 3);
    if (hovered >= 0) ring(hovered, this.theme.highlight, 2);
  }

  private drawLabels(ctx: CanvasRenderingContext2D, model: GraphModel): void {
    const { k } = this.transform;
    const highlight = this.options.highlightCommunity;
    const candidates: { index: number; text: string; important: boolean }[] = [];
    const seen = new Set<number>();
    const add = (index: number, important: boolean) => {
      const node = model.nodes[index];
      if (!node || seen.has(index)) return;
      seen.add(index);
      candidates.push({
        index,
        text: index === this.youIndex ? 'You' : truncate(node.name, 28),
        important,
      });
    };
    if (this.hovered >= 0) add(this.hovered, true);
    if (this.youIndex >= 0) add(this.youIndex, true);
    if (this.focusIndex >= 0) add(this.focusIndex, true);
    for (const index of this.labelOrder) {
      if (candidates.length >= MAX_LABELS) break;
      const node = model.nodes[index];
      if (!node) continue;
      if (node.radius * k < LABEL_MIN_SCREEN_RADIUS) break; // sorted by radius: none further fit
      if (highlight !== null && node.community !== highlight) continue;
      add(index, false);
    }
    if (candidates.length === 0) return;

    ctx.font = `600 ${String(LABEL_FONT_PX)}px ${this.theme.font}`;
    ctx.textBaseline = 'middle';
    ctx.lineJoin = 'round';
    const placed: [number, number, number, number][] = [];
    for (const candidate of candidates) {
      const node = model.nodes[candidate.index];
      const point = this.screenPosition(candidate.index);
      if (!node || !point) continue;
      const textWidth = ctx.measureText(candidate.text).width;
      // Labels go to the right of their circle, or to the left where they would leave the frame.
      const offset = node.radius * k + 4;
      const fitsRight = point.x + offset + textWidth + 3 <= this.width;
      const fitsLeft = point.x - offset - textWidth - 3 >= 0;
      const left = !fitsRight && fitsLeft ? point.x - offset - textWidth : point.x + offset;
      const box: [number, number, number, number] = [
        left - 3,
        point.y - LABEL_FONT_PX / 2 - 2,
        left + textWidth + 3,
        point.y + LABEL_FONT_PX / 2 + 2,
      ];
      const offscreen = box[2] < 0 || box[0] > this.width || box[3] < 0 || box[1] > this.height;
      if (offscreen) continue;
      const overlaps = placed.some(
        (other) => box[0] < other[2] && box[2] > other[0] && box[1] < other[3] && box[3] > other[1],
      );
      if (overlaps && !candidate.important) continue;
      placed.push(box);
      ctx.lineWidth = 3;
      ctx.strokeStyle = this.theme.surface;
      ctx.strokeText(candidate.text, left, point.y);
      ctx.fillStyle = this.theme.text;
      ctx.fillText(candidate.text, left, point.y);
    }
  }

  // ---------- Internals ----------

  private resolveOptionIndices(): void {
    const model = this.model;
    const indexOf = (id: number | null) =>
      id === null || !model ? -1 : (model.indexById.get(id) ?? -1);
    this.youIndex = indexOf(this.options.youId);
    this.focusIndex = indexOf(this.options.focusId);
  }

  private ensureTree(positions: Float32Array): Quadtree<number> {
    if (!this.tree || this.treeDirty) {
      const count = positions.length / 2;
      this.tree = quadtree<number>()
        .x((i) => positions[2 * i] ?? 0)
        .y((i) => positions[2 * i + 1] ?? 0)
        .addAll(Array.from({ length: count }, (_, i) => i));
      this.treeDirty = false;
    }
    return this.tree;
  }

  private applyFit(): void {
    const positions = this.positions;
    const model = this.model;
    if (!positions || !model || this.width === 0 || this.height === 0) return;
    let minX = Number.POSITIVE_INFINITY;
    let minY = Number.POSITIVE_INFINITY;
    let maxX = Number.NEGATIVE_INFINITY;
    let maxY = Number.NEGATIVE_INFINITY;
    for (const node of model.nodes) {
      const x = positions[2 * node.index] ?? 0;
      const y = positions[2 * node.index + 1] ?? 0;
      minX = Math.min(minX, x - node.radius);
      minY = Math.min(minY, y - node.radius);
      maxX = Math.max(maxX, x + node.radius);
      maxY = Math.max(maxY, y + node.radius);
    }
    if (!Number.isFinite(minX)) return;
    const target = fitTransform({ minX, minY, maxX, maxY }, this.width, this.height);
    // Programmatic: the zoom event has no sourceEvent, so userMovedView stays unchanged.
    this.zoomBehavior.transform(select(this.canvas), target);
  }

  private setHovered(index: number): void {
    if (index === this.hovered) return;
    this.hovered = index;
    const node = index >= 0 ? (this.model?.nodes[index] ?? null) : null;
    this.canvas.style.cursor = node ? 'pointer' : '';
    this.callbacks.onHover(node, node ? this.screenPosition(index) : null);
    this.requestDraw();
  }

  private readonly handlePointerMove = (event: PointerEvent): void => {
    if (this.interacting) return;
    const rect = this.canvas.getBoundingClientRect();
    const node = this.nodeAtPoint(event.clientX - rect.left, event.clientY - rect.top);
    this.setHovered(node ? node.index : -1);
  };

  private readonly handlePointerLeave = (): void => {
    this.setHovered(-1);
  };

  private readonly handleClick = (event: MouseEvent): void => {
    if (event.defaultPrevented) return;
    const rect = this.canvas.getBoundingClientRect();
    const node = this.nodeAtPoint(event.clientX - rect.left, event.clientY - rect.top);
    if (node) this.callbacks.onSelect(node);
  };
}
