import { ForceLayout, type LayoutRequest } from './forceLayout';
import type { LayoutWorkerRequest, LayoutWorkerResponse } from './layoutProtocol';

/**
 * Starts a layout and reports positions. Uses a dedicated Web Worker when available (keeps
 * the main thread free for a few thousand nodes) and otherwise runs in time slices on the
 * main thread, e.g. in tests or very old browsers.
 */

export interface LayoutCallbacks {
  /** New positions (x, y pairs by node index). `done` marks the final layout. */
  onPositions: (positions: Float32Array, done: boolean) => void;
  /** Progress 0..1 (reported in reduced-motion mode, where intermediate positions are skipped). */
  onProgress?: (progress: number) => void;
}

export interface LayoutRunner {
  start: (request: LayoutRequest, animate: boolean, callbacks: LayoutCallbacks) => void;
  stop: () => void;
  dispose: () => void;
}

function createWorkerRunner(): LayoutRunner | null {
  if (typeof Worker === 'undefined') return null;
  let worker: Worker;
  try {
    worker = new Worker(new URL('./layout.worker.ts', import.meta.url), { type: 'module' });
  } catch {
    return null;
  }
  let runId = 0;
  let callbacks: LayoutCallbacks | null = null;
  worker.onmessage = (event: MessageEvent<LayoutWorkerResponse>) => {
    const message = event.data;
    if (message.runId !== runId || !callbacks) return;
    if (message.type === 'positions') callbacks.onPositions(message.positions, message.done);
    else callbacks.onProgress?.(message.progress);
  };
  const send = (message: LayoutWorkerRequest) => {
    worker.postMessage(message);
  };
  return {
    start(request, animate, next) {
      runId++;
      callbacks = next;
      send({ type: 'start', runId, request, animate });
    },
    stop() {
      runId++;
      callbacks = null;
      send({ type: 'stop' });
    },
    dispose() {
      callbacks = null;
      worker.terminate();
    },
  };
}

function createInlineRunner(): LayoutRunner {
  let timer: ReturnType<typeof setTimeout> | null = null;
  const stop = () => {
    if (timer !== null) clearTimeout(timer);
    timer = null;
  };
  return {
    start(request, animate, callbacks) {
      stop();
      const layout = new ForceLayout(request);
      const slice = () => {
        const done = layout.step(12);
        if (done || animate) callbacks.onPositions(layout.positions(), done);
        else callbacks.onProgress?.(layout.progress);
        timer = done ? null : setTimeout(slice, 16);
      };
      timer = setTimeout(slice, 0);
    },
    stop,
    dispose: stop,
  };
}

export function createLayoutRunner(): LayoutRunner {
  return createWorkerRunner() ?? createInlineRunner();
}
