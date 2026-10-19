#include "sg/graph/affinity.hpp"

#include <cmath>

namespace sg::graph {

double affinity(const Profile& a, const Profile& b) noexcept {
  double similarity = 1.0 - std::abs(a.influence - b.influence) / kInfluenceScale;
  // Clamp to [0, 1]; the negated comparison also maps NaN to 0.
  if (!(similarity > 0.0)) {
    similarity = 0.0;
  } else if (similarity > 1.0) {
    similarity = 1.0;
  }
  const double category = a.category == b.category ? kAffinityCategoryWeight : 0.0;
  const double branch = a.branch == b.branch ? kAffinityBranchWeight : 0.0;
  return category + branch + kAffinityInfluenceWeight * similarity;
}

double connection_strength(const Profile& a, const Profile& b) noexcept {
  return std::round(affinity(a, b) * 1000.0) / 1000.0;
}

double louvain_weight(double strength) noexcept { return 0.5 + 0.5 * strength; }

}  // namespace sg::graph
