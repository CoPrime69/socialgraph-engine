import { describe, expect, it, vi } from 'vitest';
import { graphVersionStore } from './graphVersion';

describe('graphVersionStore', () => {
  it('only moves forward and notifies subscribers of changes', () => {
    const listener = vi.fn();
    const unsubscribe = graphVersionStore.subscribe(listener);
    expect(graphVersionStore.minVersionParam()).toBeUndefined();

    graphVersionStore.observe(5);
    graphVersionStore.observe(3);
    graphVersionStore.observe(5);
    graphVersionStore.observe(Number.NaN);
    graphVersionStore.observe(undefined);
    expect(graphVersionStore.get()).toBe(5);
    expect(graphVersionStore.minVersionParam()).toBe(5);
    expect(listener).toHaveBeenCalledTimes(1);

    graphVersionStore.reset();
    graphVersionStore.reset();
    expect(graphVersionStore.get()).toBe(0);
    expect(listener).toHaveBeenCalledTimes(2);

    unsubscribe();
    graphVersionStore.observe(9);
    expect(listener).toHaveBeenCalledTimes(2);
  });
});
