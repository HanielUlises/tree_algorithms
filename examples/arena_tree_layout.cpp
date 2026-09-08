// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The layout objection, answered.
//
// Every other representation in this repository spends a pointer per
// edge. The reasonable objection is that the algorithm family therefore
// assumes a linked structure — that it has nothing to say about the flat
// arena an implementer would actually ship, where nodes sit contiguously
// in one array and a child is an index rather than an address.
//
// This example folds the same tree twice, over two layouts that share no
// storage strategy at all: BinaryTree, whose edges are shared_ptr, and
// ArenaTree, which contains no pointers whatsoever. The algebras are
// written once. What changes between the two folds is the projection —
// one hands back child pointers, the other hands back cursors — and
// nothing else. The verb, the algebras, and the answers are identical.

#include <beman/tree_algorithms/arena_tree.hpp>
#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/fold_map_lookup.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <algorithm>
#include <functional>
#include <print>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

using beman::tree_algorithms::arena_root;
using beman::tree_algorithms::arena_tree_project;
using beman::tree_algorithms::ArenaAbsent;
using beman::tree_algorithms::ArenaNode;
using beman::tree_algorithms::ArenaTree;
using beman::tree_algorithms::ArenaTreeF;
using beman::tree_algorithms::binary_tree_project;
using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::overloaded;

namespace {

// The one fmap both folds use: resolved from the layer type, so the
// same expression serves two different layers.
const auto fmap_fn = [](auto&& fn, const auto& layer) {
    using Layer = std::remove_cvref_t<decltype(layer)>;
    return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
};

// ---------------------------------------------------------------------
// Two algebras per question — one per layer type, same arithmetic.
// ---------------------------------------------------------------------
//
// The layer types differ because the representations differ: a
// BinaryTreeF layer holds nullable child slots, an ArenaTreeF layer is a
// variant that is either absent or a node. The *algebra* is the same
// idea in both, and neither one mentions recursion, allocation, or the
// storage underneath.

const auto depth_linked = [](const BinaryTreeF<int, int>& layer) -> int {
    int left  = layer.left ? *layer.left : 0;
    int right = layer.right ? *layer.right : 0;
    return 1 + std::max(left, right);
};

const auto depth_arena = [](const ArenaTreeF<int, int>& layer) -> int {
    return std::visit(overloaded{
                          [](const ArenaAbsent&) { return 0; },
                          [](const ArenaNode<int, int>& n) { return 1 + std::max(n.left, n.right); },
                      },
                      layer);
};

const auto flatten_linked = [](const BinaryTreeF<int, std::string>& layer) -> std::string {
    std::string left  = layer.left ? *layer.left : std::string{};
    std::string right = layer.right ? *layer.right : std::string{};
    return left + std::to_string(layer.value) + right;
};

const auto flatten_arena = [](const ArenaTreeF<int, std::string>& layer) -> std::string {
    return std::visit(overloaded{
                          [](const ArenaAbsent&) { return std::string{}; },
                          [](const ArenaNode<int, std::string>& n) {
                              return n.left + std::to_string(n.value) + n.right;
                          },
                      },
                      layer);
};

// The same seven-node shape, built into each representation:
//         4
//     2       6
//   1   3   5   7
auto build_linked() -> BinaryTree<int> {
    using T = BinaryTree<int>;
    return T::node(4, T::node(2, T::leaf(1), T::leaf(3)), T::node(6, T::leaf(5), T::leaf(7)));
}

auto build_arena() -> ArenaTree<int> {
    using T = ArenaTree<int>;
    return T::node(4, T::node(2, T::leaf(1), T::leaf(3)), T::node(6, T::leaf(5), T::leaf(7)));
}

// Is the arena really one contiguous block? Ask the addresses, not the
// documentation.
auto nodes_are_contiguous(const ArenaTree<int>& tree) -> bool {
    const auto& nodes = tree.nodes();
    if (nodes.size() < 2U) {
        return true;
    }
    const auto* base = nodes.data();
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        if (&nodes[i] != base + i) {
            return false;
        }
    }
    return true;
}

} // namespace

auto main() -> int {
    auto linked = build_linked();
    auto arena  = build_arena();

    // ---- Fold each in its own representation -------------------------
    auto linked_depth = fold_with<int>(depth_linked, fmap_fn, binary_tree_project, &linked);
    auto arena_depth  = fold_with<int>(depth_arena, fmap_fn, arena_tree_project, arena_root(arena));

    auto linked_flat = fold_with<std::string>(flatten_linked, fmap_fn, binary_tree_project, &linked);
    auto arena_flat  = fold_with<std::string>(flatten_arena, fmap_fn, arena_tree_project, arena_root(arena));

    // ---- The elementwise fold, through typeclass lookup --------------
    auto cursor      = arena_root(arena);
    auto linked_sum  = fold_map<int>(std::identity{}, std::plus{}, 0, linked);
    auto arena_sum   = fold_map<int>(std::identity{}, std::plus{}, 0, cursor);

    std::println("shared_ptr tree   depth={}  in-order={}  sum={}", linked_depth, linked_flat, linked_sum);
    std::println("arena tree        depth={}  in-order={}  sum={}", arena_depth, arena_flat, arena_sum);
    std::println("");
    std::println("arena node count: {}", arena.size());
    std::println("arena root index: {}", arena.root());
    std::println("nodes contiguous: {}", nodes_are_contiguous(arena));

    // The arena stores children as indices; print the raw table to make
    // the absence of pointers concrete.
    std::println("");
    std::println("  idx  value  left  right");
    for (std::size_t i = 0; i < arena.nodes().size(); ++i) {
        const auto& node  = arena.nodes()[i];
        auto        show  = [](ArenaTree<int>::index_type ix) {
            return ix == ArenaTree<int>::npos ? std::string{"-"} : std::to_string(ix);
        };
        std::println("  {:>3}  {:>5}  {:>4}  {:>5}", i, node.d_value, show(node.d_left), show(node.d_right));
    }

    bool ok = (linked_depth == arena_depth) && (linked_flat == arena_flat) && (linked_sum == arena_sum) &&
              (linked_flat == "1234567") && (arena.size() == 7U) && nodes_are_contiguous(arena);

    std::println("");
    std::println("both layouts agree: {}", ok);
    return ok ? 0 : 1;
}
