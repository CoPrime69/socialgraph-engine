import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { topGraph, jonSkeet, marcGravell } from '../../test/fixtures';
import { canvasCalls, clearCanvasCalls } from '../../test/domShims';
import { buildGraphModel, type GraphModel } from './model';
import {
  edgeAlpha,
  fitTransform,
  GraphRenderer,
  MAX_ZOOM,
  MIN_ZOOM,
  wheelDelta,
  zoomFilter,
  type RendererCallbacks,
} from './renderer';
import { readGraphTheme } from './theme';

describe('renderer helpers', () => {
  it('fades edges as more are drawn', () => {
    expect([100, 1000, 5000, 20000].map(edgeAlpha)).toEqual([0.5, 0.35, 0.22, 0.12]);
  });

  it('zooms gently for wheels and faster for trackpad pinches', () => {
    const wheel = (init: Partial<WheelEvent>) => wheelDelta(init as WheelEvent);
    expect(wheel({ deltaY: -100, deltaMode: 0, ctrlKey: true })).toBeCloseTo(0.2);
    expect(wheel({ deltaY: -10, deltaMode: 0, ctrlKey: true })).toBeCloseTo(0.2);
    expect(wheel({ deltaY: 3, deltaMode: 1, ctrlKey: false })).toBeCloseTo(-0.15);
    expect(wheel({ deltaY: -1, deltaMode: 2, ctrlKey: false })).toBe(0.5);
    expect(wheel({ deltaY: 5000, deltaMode: 0, ctrlKey: false })).toBe(-0.5);
  });

  it('never captures plain scrolling or one-finger swipes', () => {
    const event = (type: string, init: object) => Object.assign(new Event(type), init);
    expect(zoomFilter(event('wheel', { ctrlKey: false, metaKey: false }))).toBe(false);
    expect(zoomFilter(event('wheel', { ctrlKey: true, metaKey: false }))).toBe(true);
    expect(zoomFilter(event('wheel', { ctrlKey: false, metaKey: true }))).toBe(true);
    expect(zoomFilter(event('touchstart', { touches: [{}] }))).toBe(false);
    expect(zoomFilter(event('touchstart', { touches: [{}, {}] }))).toBe(true);
    expect(zoomFilter(event('mousedown', { ctrlKey: false, button: 0 }))).toBe(true);
    expect(zoomFilter(event('mousedown', { ctrlKey: false, button: 2 }))).toBe(false);
    expect(zoomFilter(event('mousedown', { ctrlKey: true, button: 0 }))).toBe(false);
  });

  it('fits bounds into the viewport with padding and limits', () => {
    const t = fitTransform({ minX: -100, minY: -50, maxX: 100, maxY: 50 }, 456, 256);
    expect(t.k).toBeCloseTo(2);
    expect(t.apply([0, 0])).toEqual([228, 128]);
    expect(fitTransform({ minX: 0, minY: 0, maxX: 1, maxY: 1 }, 800, 600).k).toBe(2.5);
    expect(fitTransform({ minX: 0, minY: 0, maxX: 1e7, maxY: 1e7 }, 800, 600).k).toBe(MIN_ZOOM);
  });
});

describe('GraphRenderer', () => {
  let canvas: HTMLCanvasElement;
  let callbacks: RendererCallbacks;
  let renderer: GraphRenderer;
  let model: GraphModel;

  // Two people 100 units apart (Jon Skeet at the origin), the rest further out.
  const positions = (count: number) => {
    const values = new Float32Array(count * 2);
    for (let i = 0; i < count; i++) {
      values[2 * i] = i * 100;
      values[2 * i + 1] = i % 2 === 0 ? 0 : 40;
    }
    return values;
  };

  beforeEach(() => {
    vi.useFakeTimers({ toFake: ['requestAnimationFrame', 'cancelAnimationFrame'] });
    canvas = document.createElement('canvas');
    document.body.append(canvas);
    callbacks = { onHover: vi.fn(), onSelect: vi.fn() };
    renderer = new GraphRenderer(canvas, readGraphTheme(canvas), callbacks);
    const data = topGraph();
    model = buildGraphModel(data.nodes, data.edges, new Map([[3, 1]]));
    renderer.resize(800, 400, 2);
    renderer.setModel(model);
    renderer.setOptions({
      highlightCommunity: null,
      youId: jonSkeet.id,
      focusId: marcGravell.id,
      showAllEdges: false,
    });
    renderer.setPositions(positions(model.nodes.length));
  });

  afterEach(() => {
    renderer.destroy();
    canvas.remove();
    vi.useRealTimers();
  });

  const drawn = (name: string) => canvasCalls(canvas).filter((call) => call.name === name);

  it('sizes the backing store for the device pixel ratio (at most 2×)', () => {
    expect(canvas.width).toBe(1600);
    expect(canvas.height).toBe(800);
    renderer.resize(100, 50, 3);
    expect(canvas.width).toBe(200);
  });

  it('draws edges, nodes and labels once per animation frame', () => {
    clearCanvasCalls(canvas);
    renderer.requestDraw();
    renderer.requestDraw();
    vi.advanceTimersToNextFrame();
    expect(drawn('clearRect')).toHaveLength(1);
    expect(drawn('lineTo').length).toBeGreaterThanOrEqual(model.allStrengths.length);
    expect(drawn('arc').length).toBeGreaterThanOrEqual(model.nodes.length);
    const labels = drawn('fillText').map((call) => call.args[0]);
    expect(labels).toContain('You');
    expect(labels).toContain('Marc Gravell');
  });

  it('keeps labels inside the frame', () => {
    // The last person is drawn at the right edge; hovering them always labels them.
    const last = model.nodes.length - 1;
    const point = renderer.screenPosition(last) ?? { x: 0, y: 0 };
    canvas.dispatchEvent(new MouseEvent('pointermove', { clientX: point.x, clientY: point.y }));
    clearCanvasCalls(canvas);
    renderer.draw();
    const name = model.nodes[last]!.name;
    const label = drawn('fillText').find((call) => call.args[0] === name);
    expect(label).toBeDefined();
    const x = label!.args[1] as number;
    // The test canvas measures 7 px per character.
    expect(x + name.length * 7).toBeLessThanOrEqual(800);
    expect(x).toBeLessThan(point.x);

    // Labels that fit stay on the right.
    const you = renderer.screenPosition(0) ?? { x: 0, y: 0 };
    const youLabel = drawn('fillText').find((call) => call.args[0] === 'You');
    expect(youLabel!.args[1] as number).toBeGreaterThan(you.x);
  });

  it('dims everyone outside a highlighted community', () => {
    renderer.setOptions({ highlightCommunity: 3, youId: null, focusId: null, showAllEdges: true });
    clearCanvasCalls(canvas);
    renderer.draw();
    expect(drawn('fill').length).toBeGreaterThan(1);
    expect(drawn('stroke').length).toBeGreaterThan(2);
  });

  it('fits the layout into view and zooms with the controls', () => {
    const fitted = renderer.zoomLevel;
    expect(fitted).toBeGreaterThan(MIN_ZOOM);
    renderer.zoomBy(1.5);
    expect(renderer.zoomLevel).toBeCloseTo(fitted * 1.5);
    renderer.zoomBy(1000);
    expect(renderer.zoomLevel).toBe(MAX_ZOOM);
    // A new layout no longer refits once the user has zoomed…
    renderer.setPositions(positions(model.nodes.length));
    expect(renderer.zoomLevel).toBe(MAX_ZOOM);
    // …until they ask for it.
    renderer.fitToView();
    expect(renderer.zoomLevel).toBeCloseTo(fitted);
  });

  it('finds the node under the pointer and reports hover and selection', () => {
    const jon = renderer.screenPosition(0);
    expect(jon).not.toBeNull();
    const point = jon ?? { x: 0, y: 0 };
    expect(renderer.nodeAtPoint(point.x + 1, point.y)?.id).toBe(jonSkeet.id);
    expect(renderer.nodeAtPoint(point.x, point.y + 150)).toBeNull();

    canvas.dispatchEvent(new MouseEvent('pointermove', { clientX: point.x, clientY: point.y }));
    expect(callbacks.onHover).toHaveBeenLastCalledWith(
      expect.objectContaining({ id: jonSkeet.id }),
      expect.objectContaining({ x: point.x, y: point.y }),
    );
    expect(canvas.style.cursor).toBe('pointer');

    canvas.dispatchEvent(new MouseEvent('click', { clientX: point.x, clientY: point.y }));
    expect(callbacks.onSelect).toHaveBeenCalledWith(expect.objectContaining({ id: jonSkeet.id }));

    canvas.dispatchEvent(new MouseEvent('pointerleave'));
    expect(callbacks.onHover).toHaveBeenLastCalledWith(null, null);
    expect(canvas.style.cursor).toBe('');
  });

  it('ignores clicks on empty space and positions of the wrong size', () => {
    canvas.dispatchEvent(new MouseEvent('click', { clientX: 799, clientY: 1 }));
    expect(callbacks.onSelect).not.toHaveBeenCalled();
    const before = renderer.screenPosition(1);
    renderer.setPositions(new Float32Array(4));
    expect(renderer.screenPosition(1)).toEqual(before);
    expect(renderer.screenPosition(99)).toBeNull();
  });

  it('keeps positions when only colours change, and drops them for new data', () => {
    renderer.setModel(buildGraphModel(model.nodes, [], new Map()), true);
    expect(renderer.screenPosition(0)).not.toBeNull();
    renderer.setModel(buildGraphModel(model.nodes.slice(0, 3), [], new Map()));
    expect(renderer.screenPosition(0)).toBeNull();
    expect(renderer.nodeAtPoint(10, 10)).toBeNull();
  });

  it('stops drawing after destroy', () => {
    renderer.destroy();
    clearCanvasCalls(canvas);
    renderer.requestDraw();
    renderer.draw();
    expect(canvasCalls(canvas)).toHaveLength(0);
  });
});
