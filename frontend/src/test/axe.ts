import axe from 'axe-core';

export interface AxeViolation {
  id: string;
  impact: string | null | undefined;
  targets: string[];
}

/**
 * Runs axe-core (WCAG 2.x A/AA rules) on `container`. Colour contrast is checked in the
 * browser end-to-end tests instead: jsdom does not compute styles from the stylesheets.
 */
export async function axeViolations(container: Element = document.body): Promise<AxeViolation[]> {
  const results = await axe.run(container, {
    runOnly: { type: 'tag', values: ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa'] },
    rules: { 'color-contrast': { enabled: false } },
    resultTypes: ['violations'],
  });
  return results.violations.map((violation) => ({
    id: violation.id,
    impact: violation.impact,
    targets: violation.nodes.map((node) => node.target.join(' ')),
  }));
}
