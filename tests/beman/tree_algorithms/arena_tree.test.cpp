// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/arena_tree.hpp>
#include <beman/tree_algorithms/arena_tree.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/fold_map.hpp>
#include <beman/tree_algorithms/fold_map_lookup.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

using beman::tree_algorithms::arena_root;
using beman::tree_algorithms::arena_tree_embed;
using beman::tree_algorithms::arena_tree_layer_fold_map;
using beman::tree_algorithms::arena_tree_project;
using beman::tree_algorithms::ArenaAbsent;
using beman::tree_algorithms::ArenaCursor;
using beman::tree_algorithms::ArenaNode;
using beman::tree_algorithms::ArenaTree;
using beman::tree_algorithms::ArenaTreeF;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::unfold_with;

namespace {

using Tree   = ArenaTree<int>;
using Cursor = ArenaCursor<int>;

inline const auto fmap_fn = [](auto&& fn, const auto& layer) {
    using Layer = std::remove_cvref_t<decltype(layer)>;
    return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
};

// Order-and-shape-pinning algebra: a node parenthesizes its children
// around its own value, so both element order and tree shape are
// distinguished (Decision 7 / DEV-01).
inline auto shape_algebra = [](const ArenaTreeF<int, std::string>& layer) -> std::string {
    return std::visit(overloaded{
                          [](const ArenaAbsent&) -> std::string { return "()"; },
                          [](const ArenaNode<int, std::string>& n) {
                              return "(" + n.left + " " + std::to_string(n.value) + " " + n.right + ")";
                          },
                      },
                      layer);
};

inline auto arena_shape(const Tree& t) -> std::string {
    return fold_with<std::string>(shape_algebra, fmap_fn, arena_tree_project, arena_root(t));
}

// 2 over (1, 3): in-order 1 2 3.
inline auto sample() -> Tree { return Tree::node(2, Tree::leaf(1), Tree::leaf(3)); }

} // namespace

TEST_CASE("ArenaTree stores nodes contiguously with no pointers", "[arena_tree]") {
    auto tree = sample();

    REQUIRE_FALSE(tree.is_empty());
    REQUIRE(tree.size() == 3U);
    REQUIRE(tree.nodes().size() == 3U);

    // The root is a real index into the arena, and its children are
    // indices too — not addresses.
    const auto& root = tree.at(tree.root());
    REQUIRE(root.d_value == 2);
    REQUIRE(root.d_left != Tree::npos);
    REQUIRE(root.d_right != Tree::npos);
    REQUIRE(tree.at(root.d_left).d_value == 1);
    REQUIRE(tree.at(root.d_right).d_value == 3);

    // Leaves have no children.
    REQUIRE(tree.at(root.d_left).d_left == Tree::npos);
    REQUIRE(tree.at(root.d_left).d_right == Tree::npos);
}

TEST_CASE("The empty arena tree has no root", "[arena_tree]") {
    auto tree = Tree::empty();

    REQUIRE(tree.is_empty());
    REQUIRE(tree.size() == 0U);
    REQUIRE(tree.root() == Tree::npos);
    REQUIRE(arena_shape(tree) == "()");
}

TEST_CASE("ArenaTreeF has a functor instance that maps both children", "[arena_tree]") {
    STATIC_REQUIRE(has_functor_instance<ArenaTreeF<int, int> >);

    ArenaTreeF<int, int> layer = ArenaNode<int, int>{7, 1, 2};
    auto mapped = functor_typeclass<ArenaTreeF<int, int> >.fmap([](int child) { return child * 10; }, layer);

    auto& node = std::get<ArenaNode<int, int> >(mapped);
    REQUIRE(node.value == 7); // the element is untouched
    REQUIRE(node.left == 10);
    REQUIRE(node.right == 20);

    // Absent layers pass through.
    ArenaTreeF<int, int> absent = ArenaAbsent{};
    auto                 id     = functor_typeclass<ArenaTreeF<int, int> >.fmap([](int c) { return c; }, absent);
    REQUIRE(std::holds_alternative<ArenaAbsent>(id));
}

TEST_CASE("fold_with folds an arena tree in its own representation", "[arena_tree]") {
    auto tree = sample();

    auto sum_algebra = [](const ArenaTreeF<int, int>& layer) -> int {
        return std::visit(overloaded{
                              [](const ArenaAbsent&) { return 0; },
                              [](const ArenaNode<int, int>& n) { return n.left + n.value + n.right; },
                          },
                          layer);
    };

    REQUIRE(fold_with<int>(sum_algebra, fmap_fn, arena_tree_project, arena_root(tree)) == 6);
}

TEST_CASE("fold_with observes in-order traversal", "[arena_tree]") {
    // Shape is pinned, not just the element multiset: a left-leaning and
    // a right-leaning tree over the same elements differ.
    auto balanced = sample();
    REQUIRE(arena_shape(balanced) == "((() 1 ()) 2 (() 3 ()))");

    auto right_spine = Tree::node(1, Tree::empty(), Tree::node(2, Tree::empty(), Tree::leaf(3)));
    REQUIRE(arena_shape(right_spine) == "(() 1 (() 2 (() 3 ())))");

    // Same in-order elements, different shape.
    auto in_order = [](const Tree& t) {
        auto flatten = [](const ArenaTreeF<int, std::string>& layer) -> std::string {
            return std::visit(overloaded{
                                  [](const ArenaAbsent&) -> std::string { return ""; },
                                  [](const ArenaNode<int, std::string>& n) {
                                      return n.left + std::to_string(n.value) + n.right;
                                  },
                              },
                              layer);
        };
        return fold_with<std::string>(flatten, fmap_fn, arena_tree_project, arena_root(t));
    };
    REQUIRE(in_order(balanced) == "123");
    REQUIRE(in_order(right_spine) == "123");
    REQUIRE(arena_shape(balanced) != arena_shape(right_spine));
}

TEST_CASE("The layer fold combines in order", "[arena_tree]") {
    // A non-commutative combine observes the left / value / right
    // contract.
    ArenaTreeF<int, std::string> layer = ArenaNode<int, std::string>{2, "L", "R"};

    auto folded = arena_tree_layer_fold_map(
        [](int v) { return std::to_string(v); }, std::plus<std::string>{}, std::string{}, layer);

    REQUIRE(folded == "L2R");
}

TEST_CASE("fold_map resolves the arena projection through lookup", "[arena_tree]") {
    auto tree   = sample();
    auto cursor = arena_root(tree);

    REQUIRE(fold_map<int>(std::identity{}, std::plus{}, 0, cursor) == 6);

    // Non-commutative combine again, this time through the lookup path.
    auto text = fold_map<std::string>(
        [](int v) { return std::to_string(v); }, std::plus<std::string>{}, std::string{}, cursor);
    REQUIRE(text == "123");
}

TEST_CASE("unfold_with builds an arena from a seed", "[arena_tree]") {
    // Right spine 1, 2, 3; a seed past the end is absent.
    auto coalgebra = [](int n) -> ArenaTreeF<int, int> {
        if (n > 3) {
            return ArenaAbsent{};
        }
        return ArenaNode<int, int>{n, 4, n + 1}; // 4 seeds the absent left
    };

    auto built = unfold_with<Tree>(coalgebra, fmap_fn, arena_tree_embed, 1);

    REQUIRE(built.size() == 3U);
    REQUIRE(arena_shape(built) == "(() 1 (() 2 (() 3 ())))");

    // Built by the embedding, the arena is still flat and still
    // index-addressed.
    auto cursor = arena_root(built);
    REQUIRE(fold_map<int>(std::identity{}, std::plus{}, 0, cursor) == 6);
}

TEST_CASE("Absorbing subtrees rebases their indices correctly", "[arena_tree]") {
    // The join copies both child arenas into one; every index in the
    // result must address the right node. A deeper tree makes a
    // rebasing slip visible.
    auto left  = Tree::node(2, Tree::leaf(1), Tree::leaf(3));
    auto right = Tree::node(6, Tree::leaf(5), Tree::leaf(7));
    auto tree  = Tree::node(4, std::move(left), std::move(right));

    REQUIRE(tree.size() == 7U);
    REQUIRE(arena_shape(tree) == "(((() 1 ()) 2 (() 3 ())) 4 ((() 5 ()) 6 (() 7 ())))");

    auto cursor = arena_root(tree);
    REQUIRE(fold_map<int>(std::identity{}, std::plus{}, 0, cursor) == 28);
}

TEST_CASE("An arena tree copies as one contiguous block", "[arena_tree]") {
    auto tree = sample();
    auto copy = tree;

    // The copy is independent storage but identical structure — and it
    // holds no pointers back into the original.
    REQUIRE(copy.size() == tree.size());
    REQUIRE(arena_shape(copy) == arena_shape(tree));
    REQUIRE(copy.nodes().data() != tree.nodes().data());
}
