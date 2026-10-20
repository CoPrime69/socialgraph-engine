import type { Taxonomy } from '../api/types';

/** Branches of `category` in display order, or every branch when no category is chosen. */
export function branchesFor(taxonomy: Taxonomy, category: string): string[] {
  if (category === '') return taxonomy.branches;
  return taxonomy.branches.filter((branch) => taxonomy.branch_categories[branch] === category);
}
