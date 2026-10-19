import { useEffect, useId, useMemo, useRef, useState } from 'react';
import type { GraphResponse } from '../../api/types';
import { IconButton } from '../../components/Button';
import { usePrefersDarkScheme, usePrefersReducedMotion } from '../../hooks/useMediaQuery';
import { formatInteger, formatSignificant } from '../../lib/format';
import type { LayoutRequest } from './forceLayout';
import { createLayoutRunner, type LayoutRunner } from './layoutRunner';
import { buildGraphModel, type GraphModel, type ModelNode } from './model';
import { communityLabel, type PaletteSlot } from './palette';
import { GraphRenderer, type GraphPoint } from './renderer';
import { readGraphTheme } from './theme';
import styles from './NetworkGraph.module.css';

/** Fixed seed: the same data always produces the same picture. */
const LAYOUT_SEED = 20081;

interface NetworkGraphProps {
  data: GraphResponse;
  slots: ReadonlyMap<number, PaletteSlot>;
  youId: number | null;
  focusId: number | null;
  highlightCommunity: number | null;
  showAllEdges: boolean;
  /** Accessible summary of what the picture shows. */
  label: string;
  onSelectNode: (node: ModelNode) => void;
  size?: 'md' | 'lg';
}

interface LayoutSnapshot {
  nodes: readonly ModelNode[];
  positions: Float32Array;
}

interface HoverState {
  node: ModelNode;
  point: GraphPoint;
  /** Show the tooltip to the left of the node (it is in the right half of the frame). */
  flip: boolean;
}

function layoutRequest(
  model: GraphModel,
  previous: LayoutSnapshot | null,
  pinnedId: number | null,
): LayoutRequest {
  const n = model.nodes.length;
  const initial = new Float32Array(n * 2).fill(Number.NaN);
  if (previous) {
    const previousIndex = new Map(previous.nodes.map((node, index) => [node.id, index]));
    for (const node of model.nodes) {
      const index = previousIndex.get(node.id);
      if (index === undefined) continue;
      initial[2 * node.index] = previous.positions[2 * index] ?? Number.NaN;
      initial[2 * node.index + 1] = previous.positions[2 * index + 1] ?? Number.NaN;
    }
  }
  return {
    nodeCount: n,
    radius: Float32Array.from(model.nodes, (node) => node.radius),
    community: Int32Array.from(model.nodes, (node) => node.community),
    // Laying out the backbone keeps dense views untangled; drawing all edges is a view option.
    edges: model.backboneEdges,
    strengths: model.backboneStrengths,
    initial,
    pinned: pinnedId === null ? -1 : (model.indexById.get(pinnedId) ?? -1),
    seed: LAYOUT_SEED,
  };
}

/**
 * Force-directed network drawn on a canvas. Layout runs in a Web Worker and is animated unless
 * the viewer prefers reduced motion (then only the settled layout is shown). Pointer users get
 * a tooltip and click-through to profiles; the same data is available as a table (GraphTable).
 */
export function NetworkGraph({
  data,
  slots,
  youId,
  focusId,
  highlightCommunity,
  showAllEdges,
  label,
  onSelectNode,
  size = 'lg',
}: NetworkGraphProps) {
  const frameRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const rendererRef = useRef<GraphRenderer | null>(null);
  const runnerRef = useRef<LayoutRunner | null>(null);
  const lastLayoutRef = useRef<LayoutSnapshot | null>(null);
  const renderedDataRef = useRef<GraphResponse | null>(null);
  const onSelectRef = useRef(onSelectNode);
  const hintId = useId();

  const reducedMotion = usePrefersReducedMotion();
  const dark = usePrefersDarkScheme();
  const [hover, setHover] = useState<HoverState | null>(null);
  const [settledFor, setSettledFor] = useState<GraphResponse | null>(null);
  const [drawnFor, setDrawnFor] = useState<GraphResponse | null>(null);
  const [progress, setProgress] = useState(0);

  const model = useMemo(() => buildGraphModel(data.nodes, data.edges, slots), [data, slots]);
  const modelRef = useRef(model);

  useEffect(() => {
    onSelectRef.current = onSelectNode;
  }, [onSelectNode]);

  // Renderer and layout runner live as long as the component.
  useEffect(() => {
    const canvas = canvasRef.current;
    const frame = frameRef.current;
    if (!canvas || !frame) return;
    const renderer = new GraphRenderer(canvas, readGraphTheme(frame), {
      onHover: (node, point) => {
        setHover(node && point ? { node, point, flip: point.x > frame.clientWidth / 2 } : null);
      },
      onSelect: (node) => {
        onSelectRef.current(node);
      },
    });
    rendererRef.current = renderer;
    const runner = createLayoutRunner();
    runnerRef.current = runner;

    const measure = () => {
      renderer.resize(frame.clientWidth, frame.clientHeight);
    };
    measure();
    let observer: ResizeObserver | null = null;
    if (typeof ResizeObserver === 'function') {
      observer = new ResizeObserver(measure);
      observer.observe(frame);
    }
    return () => {
      observer?.disconnect();
      runner.dispose();
      renderer.destroy();
      rendererRef.current = null;
      runnerRef.current = null;
      renderedDataRef.current = null;
    };
  }, []);

  // New model: new colours only (same data) keep the layout; new data restarts it below.
  useEffect(() => {
    modelRef.current = model;
    const renderer = rendererRef.current;
    if (!renderer) return;
    renderer.setModel(model, renderedDataRef.current === data);
    renderedDataRef.current = data;
  }, [model, data]);

  // Layout: restarts when the data changes, warm-starting from the previous positions.
  useEffect(() => {
    const runner = runnerRef.current;
    const renderer = rendererRef.current;
    if (!runner || !renderer) return;
    const current = modelRef.current;
    runner.start(layoutRequest(current, lastLayoutRef.current, focusId), !reducedMotion, {
      onPositions: (positions, done) => {
        lastLayoutRef.current = { nodes: current.nodes, positions };
        renderer.setPositions(positions);
        setDrawnFor(data);
        if (done) setSettledFor(data);
      },
      onProgress: (value) => {
        setProgress(value);
      },
    });
    return () => {
      runner.stop();
    };
  }, [data, focusId, reducedMotion]);

  useEffect(() => {
    rendererRef.current?.setOptions({ highlightCommunity, youId, focusId, showAllEdges });
  }, [highlightCommunity, youId, focusId, showAllEdges]);

  useEffect(() => {
    const frame = frameRef.current;
    if (frame) rendererRef.current?.setTheme(readGraphTheme(frame));
  }, [dark]);

  const settled = settledFor === data;
  const drawn = drawnFor === data;
  const hovered = hover && model.nodes[hover.node.index]?.id === hover.node.id ? hover : null;

  return (
    <div className={styles.wrapper}>
      <div ref={frameRef} className={`${styles.frame} ${styles[size]}`}>
        <canvas
          ref={canvasRef}
          className={styles.canvas}
          role="img"
          aria-label={label}
          aria-describedby={hintId}
        />
        {!drawn ? (
          <div className={styles.overlay} role="status">
            <span>Arranging {formatInteger(model.nodes.length)} people…</span>
            <progress
              className={styles.progress}
              max={1}
              value={reducedMotion ? progress : undefined}
              aria-label="Layout progress"
            />
          </div>
        ) : !settled ? (
          <span className="visually-hidden" role="status">
            Arranging the network…
          </span>
        ) : null}
        {hovered ? <GraphTooltip hover={hovered} /> : null}
        <div className={styles.controls}>
          <IconButton
            icon="zoomIn"
            label="Zoom in"
            variant="secondary"
            size="sm"
            onClick={() => rendererRef.current?.zoomBy(1.5)}
          />
          <IconButton
            icon="zoomOut"
            label="Zoom out"
            variant="secondary"
            size="sm"
            onClick={() => rendererRef.current?.zoomBy(1 / 1.5)}
          />
          <IconButton
            icon="fit"
            label="Fit graph to view"
            variant="secondary"
            size="sm"
            onClick={() => rendererRef.current?.fitToView()}
          />
        </div>
      </div>
      <p id={hintId} className={styles.hint}>
        Drag to pan. Zoom with the buttons, Ctrl or ⌘ + scroll, or a pinch. Select a person to open
        their profile.
      </p>
    </div>
  );
}

function GraphTooltip({ hover }: { hover: HoverState }) {
  const { node, point, flip } = hover;
  return (
    <div
      className={`${styles.tooltip} ${flip ? styles.flip : ''}`}
      style={{ left: point.x, top: point.y }}
      aria-hidden="true"
    >
      <p className={styles.tooltipName}>{node.name}</p>
      <p className={styles.tooltipHandle}>@{node.handle}</p>
      <dl className={styles.tooltipFacts}>
        <dt>Reputation</dt>
        <dd>{formatInteger(node.reputation)}</dd>
        <dt>Connections</dt>
        <dd>{formatInteger(node.degree)}</dd>
        <dt>PageRank</dt>
        <dd>{formatSignificant(node.pagerank)}</dd>
        <dt>Community</dt>
        <dd>
          <span className="community-swatch" data-slot={node.slot} />
          {communityLabel(node.community)}
        </dd>
      </dl>
    </div>
  );
}
