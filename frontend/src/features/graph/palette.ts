/**
 * Community colours follow the validated categorical palette (tokens.css `--series-1..8`).
 * Hues are assigned in a fixed order to the largest communities (size desc, then id asc) and
 * never cycled: every community beyond the eighth folds into a neutral "Other" grey.
 * Because the ranking uses the whole network's community sizes, a community keeps its
 * colour on every page and when a filter changes what is visible.
 */

export const SERIES_SLOTS = 8;

/** 1..8 for coloured communities, 0 for "Other". */
export type PaletteSlot = number;

export interface CommunitySize {
  id: number;
  size: number;
}

export function assignCommunitySlots(
  communities: readonly CommunitySize[],
): Map<number, PaletteSlot> {
  const ranked = [...communities].sort((a, b) => b.size - a.size || a.id - b.id);
  const slots = new Map<number, PaletteSlot>();
  ranked.forEach((community, index) => {
    slots.set(community.id, index < SERIES_SLOTS ? index + 1 : 0);
  });
  return slots;
}

/** Counts members per community from a node list (fallback when community sizes are unavailable). */
export function communitySizesFromNodes(nodes: readonly { community: number }[]): CommunitySize[] {
  const counts = new Map<number, number>();
  for (const node of nodes) counts.set(node.community, (counts.get(node.community) ?? 0) + 1);
  return Array.from(counts, ([id, size]) => ({ id, size }));
}

export function slotCssVar(slot: PaletteSlot): string {
  return slot >= 1 && slot <= SERIES_SLOTS ? `var(--series-${slot})` : 'var(--series-other)';
}

export function communityLabel(id: number): string {
  return `Community ${id}`;
}
