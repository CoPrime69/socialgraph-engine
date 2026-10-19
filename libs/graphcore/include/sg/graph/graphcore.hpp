#pragma once

/// @file graphcore.hpp
/// Umbrella header of sg::graphcore: pure graph algorithms for the social-network analytics
/// platform (no I/O, no global state except the per-thread scratch of shortest_path).

#include "sg/graph/affinity.hpp"
#include "sg/graph/centrality.hpp"
#include "sg/graph/community.hpp"
#include "sg/graph/components.hpp"
#include "sg/graph/csr_graph.hpp"
#include "sg/graph/generator.hpp"
#include "sg/graph/parallel.hpp"
#include "sg/graph/paths.hpp"
#include "sg/graph/recommend.hpp"
