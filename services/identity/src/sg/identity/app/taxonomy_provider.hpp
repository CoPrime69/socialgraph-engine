#pragma once

/// @file taxonomy_provider.hpp
/// Source of the category/branch taxonomy used to validate registrations.

#include <memory>

#include "sg/identity/domain/taxonomy.hpp"

namespace sg::identity::app {

class TaxonomyProvider {
 public:
  virtual ~TaxonomyProvider() = default;
  /// Latest known taxonomy, or nullptr while none has been loaded. Thread-safe.
  [[nodiscard]] virtual std::shared_ptr<const domain::Taxonomy> current() const = 0;
};

/// A fixed taxonomy (tests, tools).
class StaticTaxonomyProvider final : public TaxonomyProvider {
 public:
  explicit StaticTaxonomyProvider(domain::Taxonomy taxonomy)
      : taxonomy_(std::make_shared<const domain::Taxonomy>(std::move(taxonomy))) {}
  [[nodiscard]] std::shared_ptr<const domain::Taxonomy> current() const override { return taxonomy_; }

 private:
  std::shared_ptr<const domain::Taxonomy> taxonomy_;
};

}  // namespace sg::identity::app
