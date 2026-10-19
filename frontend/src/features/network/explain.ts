import type { Connection, Recommendation } from '../../api/types';
import { formatDate, formatMonthYear, formatPercent, pluralize } from '../../lib/format';

/** Short, human reasons behind a recommendation's score (contracts §8 formula terms). */
export function explainRecommendation(recommendation: Recommendation): string[] {
  const { reasons, mutual_connections: mutual, user } = recommendation;
  const lines: string[] = [];
  if (mutual > 0) lines.push(pluralize(mutual, 'mutual connection'));
  if (reasons.same_community) lines.push('In your community');
  if (reasons.same_branch) lines.push(`Also works with ${user.branch}`);
  else if (reasons.same_category) lines.push(`Also in ${user.category}`);
  if (lines.length === 0) lines.push(`Profile similarity ${formatPercent(reasons.affinity)}`);
  return lines;
}

/** "12 interactions since Sep 2008" for imported connections, "Connected Oct 19, 2026" otherwise. */
export function describeConnection(connection: Connection): string {
  if (connection.origin === 'imported') {
    const count = pluralize(connection.interactions, 'interaction');
    return connection.first_interaction_at
      ? `${count} since ${formatMonthYear(connection.first_interaction_at)}`
      : `${count} on Stack Overflow`;
  }
  return `Connected ${formatDate(connection.connected_at)}`;
}
