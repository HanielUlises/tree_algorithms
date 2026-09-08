// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// What annotations are for.
//
// Common-subexpression elimination needs to know, for every node of an
// expression tree, whether the subtree rooted there is structurally
// identical to one seen elsewhere. That is a question about every node,
// and answering it by re-folding each subtree independently is
// quadratic: the work done for a node is thrown away and redone for its
// parent.
//
// The recursion-scheme literature answers this with a course-of-values
// fold — histo and its generalized relatives — and none of that ships
// here (Decision 1). It does not need to. annotate() folds once with an
// ordinary algebra and keeps every intermediate result, so each node
// ends up carrying the hash of its own subtree. A second pass reads
// those hashes to find the duplicates. Two linear traversals, built out
// of fold_with and nothing else.
//
// Compare examples/expression_algorithms.cpp, which folds this same tree
// to a single answer and discards everything on the way.

#include <beman/tree_algorithms/cofree.hpp>
#include <beman/tree_algorithms/expression.hpp>
#include <beman/tree_algorithms/fix.hpp>

#include <cstdint>
#include <map>
#include <print>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using beman::tree_algorithms::Add;
using beman::tree_algorithms::add_node;
using beman::tree_algorithms::annotate;
using beman::tree_algorithms::annotation_of;
using beman::tree_algorithms::Const;
using beman::tree_algorithms::const_node;
using beman::tree_algorithms::Expr;
using beman::tree_algorithms::ExprF;
using beman::tree_algorithms::fold_cofree;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::Mul;
using beman::tree_algorithms::mul_node;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::unwrap_fix;

namespace {

const auto fmap_fn = [](auto&& fn, const auto& layer) {
    using Layer = std::remove_cvref_t<decltype(layer)>;
    return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
};

// Expr is already a fixed point, so its projection is just unwrapping
// one layer.
const auto project = [](const Expr& e) -> const ExprF<Expr>& { return unwrap_fix(e); };

// ---------------------------------------------------------------------
// Pass one: an ordinary algebra, folded once.
// ---------------------------------------------------------------------

using Hash = std::uint64_t;

constexpr auto mix(Hash seed, Hash value) -> Hash {
    // Order-sensitive, so (a + b) and (b + a) hash differently, and
    // tag-seeded, so a sum and a product never collide.
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

// Nothing about this algebra knows it will be used to annotate. It is
// exactly the callable a plain fold would take: one layer whose children
// are already hashes, reduced to one hash.
const auto hash_algebra = [](const ExprF<Hash>& layer) -> Hash {
    return std::visit(overloaded{
                          [](const Const<Hash>& c) { return mix(0x1ULL, static_cast<Hash>(c.val)); },
                          [](const Add<Hash>& a) { return mix(mix(0x2ULL, *a.left), *a.right); },
                          [](const Mul<Hash>& m) { return mix(mix(0x3ULL, *m.left), *m.right); },
                      },
                      layer);
};

// ---------------------------------------------------------------------
// Pass two: read the annotations back.
// ---------------------------------------------------------------------

// Render the expression. The algebra ignores the annotation here; it is
// the plain fold, run over the annotated tree.
const auto render = [](const Hash&, const ExprF<std::string>& layer) -> std::string {
    return std::visit(overloaded{
                          [](const Const<std::string>& c) { return std::to_string(c.val); },
                          [](const Add<std::string>& a) { return "(" + *a.left + " + " + *a.right + ")"; },
                          [](const Mul<std::string>& m) { return "(" + *m.left + " * " + *m.right + ")"; },
                      },
                      layer);
};

// One entry per node: the subtree's hash, and how that subtree reads.
using Entry = std::pair<Hash, std::string>;
using Table = std::vector<Entry>;

// This is the course-of-values access. The algebra sees the node's own
// annotation — computed on the earlier pass — alongside the tables its
// children have already built. Each node appends itself last, so a
// child's own entry is always table.back().
const auto gather = [](const Hash& hash, const ExprF<Table>& layer) -> Table {
    Table out;

    auto join = [&out](const Table& left, const Table& right, const char* op) {
        out.insert(out.end(), left.begin(), left.end());
        out.insert(out.end(), right.begin(), right.end());
        return "(" + left.back().second + " " + op + " " + right.back().second + ")";
    };

    std::string text = std::visit(overloaded{
                                      [](const Const<Table>& c) { return std::to_string(c.val); },
                                      [&join](const Add<Table>& a) { return join(*a.left, *a.right, "+"); },
                                      [&join](const Mul<Table>& m) { return join(*m.left, *m.right, "*"); },
                                  },
                                  layer);

    out.emplace_back(hash, std::move(text));
    return out;
};

} // namespace

auto main() -> int {
    // (2 * 3) + ((2 * 3) + 4) — the product appears twice, built
    // independently both times, and a CSE pass should notice.
    auto expr = add_node(mul_node(const_node(2), const_node(3)),
                         add_node(mul_node(const_node(2), const_node(3)), const_node(4)));

    // One fold decorates every node with the hash of its own subtree.
    auto annotated = annotate<Hash, ExprF>(hash_algebra, fmap_fn, project, expr);

    std::println("root hash:  {:#018x}", annotation_of(annotated));
    std::println("expression: {}", fold_cofree<std::string>(render, fmap_fn, annotated));

    // Gather every subtree's hash, then report those occurring more
    // than once. No subtree is re-folded to answer this.
    auto entries = fold_cofree<Table>(gather, fmap_fn, annotated);

    std::map<Hash, std::pair<int, std::string> > counts;
    for (const auto& [hash, text] : entries) {
        auto& slot = counts[hash];
        slot.first += 1;
        slot.second = text;
    }

    std::println("");
    std::println("shared subexpressions:");
    int duplicates = 0;
    for (const auto& [hash, info] : counts) {
        if (info.first > 1) {
            ++duplicates;
            std::println("  {:#018x}  seen {}x  {}", hash, info.first, info.second);
        }
    }

    std::println("");
    std::println("nodes visited:     {}", entries.size());
    std::println("distinct subtrees: {}", counts.size());
    std::println("shared subtrees:   {}", duplicates);

    // Nine nodes, six distinct subtrees; 2, 3 and (2 * 3) each occur
    // twice.
    bool ok = (entries.size() == 9U) && (counts.size() == 6U) && (duplicates == 3) &&
              (fold_cofree<std::string>(render, fmap_fn, annotated) == "((2 * 3) + ((2 * 3) + 4))");

    std::println("as expected: {}", ok);
    return ok ? 0 : 1;
}
