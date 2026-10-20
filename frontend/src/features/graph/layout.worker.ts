import { ForceLayout, type LayoutRequest } from './forceLayout';
import type { LayoutWorkerRequest, LayoutWorkerResponse } from './layoutProtocol';

/**
 * Runs the force layout off the main thread. In animated mode it streams intermediate
 * positions ~30 times per second; in reduced-motion mode it only reports progress and the
 * final positions. A newer `start` (or a `stop`) supersedes the running layout.
 */

interface WorkerScope {
  onmessage: ((event: MessageEvent<LayoutWorkerRequest>) => void) | null;
  postMessage(message: LayoutWorkerResponse, transfer: Transferable[]): void;
}

const scope = self as unknown as WorkerScope;
const FRAME_BUDGET_MS = 24;
const POST_INTERVAL_MS = 33;

let currentRun = 0;

function post(message: LayoutWorkerResponse): void {
  scope.postMessage(message, message.type === 'positions' ? [message.positions.buffer] : []);
}

function run(runId: number, request: LayoutRequest, animate: boolean): void {
  const layout = new ForceLayout(request);
  let lastPost = 0;
  const slice = () => {
    if (runId !== currentRun) return;
    const done = layout.step(FRAME_BUDGET_MS);
    const now = performance.now();
    if (done) {
      post({ type: 'positions', runId, positions: layout.positions(), progress: 1, done: true });
      return;
    }
    if (now - lastPost >= POST_INTERVAL_MS) {
      lastPost = now;
      if (animate) {
        post({
          type: 'positions',
          runId,
          positions: layout.positions(),
          progress: layout.progress,
          done: false,
        });
      } else {
        post({ type: 'progress', runId, progress: layout.progress });
      }
    }
    // Yield so that a newer request can be received.
    setTimeout(slice, 0);
  };
  slice();
}

scope.onmessage = (event) => {
  const message = event.data;
  if (message.type === 'stop') {
    currentRun = 0;
    return;
  }
  currentRun = message.runId;
  run(message.runId, message.request, message.animate);
};
