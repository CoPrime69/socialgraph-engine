#include <gtest/gtest.h>

#include <limits>

#include "sg/graph/affinity.hpp"

namespace sg::graph {
namespace {

TEST(Affinity, ContractFormula) {
  const Profile a{1, 2, 70.0};
  // same category + same branch + identical influence
  EXPECT_DOUBLE_EQ(affinity(a, Profile{1, 2, 70.0}), 1.0);
  // same category only, |Δ| = 20 → 0.4 + 0.3·0.8
  EXPECT_DOUBLE_EQ(affinity(a, Profile{1, 3, 90.0}), 0.4 + 0.3 * 0.8);
  // same branch only, |Δ| = 50 → 0.3 + 0.3·0.5
  EXPECT_DOUBLE_EQ(affinity(a, Profile{0, 2, 20.0}), 0.3 + 0.3 * 0.5);
  // nothing in common, |Δ| = 70 → 0.3·0.3
  EXPECT_DOUBLE_EQ(affinity(a, Profile{4, 5, 0.0}), 0.3 * 0.3);
  // maximal influence gap
  EXPECT_DOUBLE_EQ(affinity(Profile{0, 0, 0.0}, Profile{1, 1, 100.0}), 0.0);
}

TEST(Affinity, IsSymmetricAndBounded) {
  for (int i = 0; i <= 100; i += 7) {
    for (int j = 0; j <= 100; j += 11) {
      const Profile a{static_cast<std::uint16_t>(i % 3), static_cast<std::uint16_t>(i % 2), i * 1.0};
      const Profile b{static_cast<std::uint16_t>(j % 3), static_cast<std::uint16_t>(j % 2), j * 1.0};
      const double ab = affinity(a, b);
      EXPECT_EQ(ab, affinity(b, a));
      EXPECT_GE(ab, 0.0);
      EXPECT_LE(ab, 1.0);
    }
  }
}

TEST(Affinity, InfluenceTermIsClampedAndNaNSafe) {
  EXPECT_DOUBLE_EQ(affinity(Profile{0, 0, -50.0}, Profile{1, 1, 150.0}), 0.0);
  EXPECT_DOUBLE_EQ(affinity(Profile{0, 0, std::numeric_limits<double>::quiet_NaN()}, Profile{0, 0, 10.0}), 0.7);
}

TEST(Affinity, ConnectionStrengthRoundsToThreeDecimals) {
  // 0.4 + 0.3·(1 − 12.3/100) = 0.6631
  EXPECT_DOUBLE_EQ(connection_strength(Profile{0, 1, 50.0}, Profile{0, 2, 62.3}), 0.663);
  // 0.3·(1 − 33.35/100) = 0.19995 → 0.2
  EXPECT_DOUBLE_EQ(connection_strength(Profile{0, 0, 0.0}, Profile{1, 1, 33.35}), 0.2);
  EXPECT_DOUBLE_EQ(connection_strength(Profile{3, 3, 42.0}, Profile{3, 3, 42.0}), 1.0);
}

TEST(Affinity, LouvainWeightMapsStrengthToHalfOne) {
  EXPECT_DOUBLE_EQ(louvain_weight(0.0), 0.5);
  EXPECT_DOUBLE_EQ(louvain_weight(1.0), 1.0);
  EXPECT_DOUBLE_EQ(louvain_weight(0.663), 0.8315);
}

}  // namespace
}  // namespace sg::graph
