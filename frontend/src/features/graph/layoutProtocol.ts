import type { LayoutRequest } from './forceLayout';

export type LayoutWorkerRequest =
  { type: 'start'; runId: number; request: LayoutRequest; animate: boolean } | { type: 'stop' };

export type LayoutWorkerResponse =
  | { type: 'positions'; runId: number; positions: Float32Array; progress: number; done: boolean }
  | { type: 'progress'; runId: number; progress: number };
