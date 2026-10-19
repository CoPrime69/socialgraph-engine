import type { Taxonomy } from '../api/types';
import { branchesFor } from '../lib/taxonomy';

/**
 * `<option>`s for a branch (primary technology) select: only the chosen category's branches,
 * or every branch grouped by category when no category is chosen (~60 branches).
 */
export function BranchOptions({ taxonomy, category }: { taxonomy: Taxonomy; category: string }) {
  if (category !== '') {
    return branchesFor(taxonomy, category).map((branch) => (
      <option key={branch} value={branch}>
        {branch}
      </option>
    ));
  }
  return taxonomy.categories.map((name) => (
    <optgroup key={name} label={name}>
      {branchesFor(taxonomy, name).map((branch) => (
        <option key={branch} value={branch}>
          {branch}
        </option>
      ))}
    </optgroup>
  ));
}
