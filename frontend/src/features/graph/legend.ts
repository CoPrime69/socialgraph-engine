import type { GraphNode } from '../../api/types';
import type { PaletteSlot } from './palette';

export interface LegendEntry {
  id: number;
  slot: PaletteSlot;
  count: number;
}

export interface LegendSummary {
  entries: LegendEntry[];
  otherCommunities: number;
  otherPeople: number;
}

/** Visible communities with a colour (by slot), plus the size of the grey "Other" group. */
export function summarizeLegend(
  nodes: readonly GraphNode[],
  slots: ReadonlyMap<number, PaletteSlot>,
): LegendSummary {
  const counts = new Map<number, number>();
  for (const node of nodes) counts.set(node.community, (counts.get(node.community) ?? 0) + 1);
  const entries: LegendEntry[] = [];
  let otherCommunities = 0;
  let otherPeople = 0;
  for (const [id, count] of counts) {
    const slot = slots.get(id) ?? 0;
    if (slot > 0) {
      entries.push({ id, slot, count });
    } else {
      otherCommunities++;
      otherPeople += count;
    }
  }
  entries.sort((a, b) => a.slot - b.slot);
  return { entries, otherCommunities, otherPeople };
}
