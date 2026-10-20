/**
 * Read-your-writes support (contracts §6).
 *
 * graph-service returns the new `graph_version` from every connection mutation. Analytics is an
 * eventually consistent read model, so every analytics request carries `min_version=<highest
 * version this tab has seen>`; analytics-service then waits (≤ 2 s) until its read model has
 * applied that version, or answers with `X-Graph-Stale: true`.
 *
 * Versions observed in analytics responses are recorded too, which gives monotonic reads when
 * requests are spread over several analytics replicas.
 */

type Listener = () => void;

let highest = 0;
const listeners = new Set<Listener>();

function emit(): void {
  for (const listener of listeners) listener();
}

export const graphVersionStore = {
  /** Highest graph version observed, or 0 when none has been seen yet. */
  get(): number {
    return highest;
  },

  /** Value for the `min_version` query parameter, or undefined when there is nothing to wait for. */
  minVersionParam(): number | undefined {
    return highest > 0 ? highest : undefined;
  },

  observe(version: number | null | undefined): void {
    if (typeof version !== 'number' || !Number.isFinite(version) || version <= highest) return;
    highest = version;
    emit();
  },

  reset(): void {
    if (highest === 0) return;
    highest = 0;
    emit();
  },

  subscribe(listener: Listener): () => void {
    listeners.add(listener);
    return () => {
      listeners.delete(listener);
    };
  },
};
