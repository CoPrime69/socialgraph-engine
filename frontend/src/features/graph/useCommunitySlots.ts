import { useMemo } from 'react';
import { useCommunities } from '../../api/queries';
import type { GraphNode } from '../../api/types';
import { assignCommunitySlots, communitySizesFromNodes, type PaletteSlot } from './palette';

/**
 * Colour slot per community. Ranked by the whole network's community sizes (communities
 * endpoint) so a community keeps its colour on every page; until that has loaded (or if it
 * fails) the visible nodes are counted instead.
 */
export function useCommunitySlots(
  fallbackNodes: readonly GraphNode[] | undefined,
): ReadonlyMap<number, PaletteSlot> {
  const communities = useCommunities();
  const items = communities.data?.items;
  return useMemo(() => {
    if (items) return assignCommunitySlots(items.map(({ id, size }) => ({ id, size })));
    return assignCommunitySlots(communitySizesFromNodes(fallbackNodes ?? []));
  }, [items, fallbackNodes]);
}
