// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/cofree.hpp>
#include <beman/tree_algorithms/cofree.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/fix.hpp>
#include <beman/tree_algorithms/rose_tree.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using beman::tree_algorithms::annotate;
using beman::tree_algorithms::annotation_of;
using beman::tree_algorithms::binary_tree_project;
using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::Cofree;
using beman::tree_algorithms::fold_cofree;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::rose;
using beman::tree_algorithms::RoseF;
using beman::tree_algorithms::RoseLayer;
using beman::tree_algorithms::RoseTreeFix;
using beman::tree_algorithms::unwrap_fix;

namespace {

template <typename A>
using IntLayer = BinaryTreeF<int, A>;

using Tree = BinaryTree<int>;

inline const auto fmap_fn = [](auto&& fn, const auto& layer) {
    using Layer = std::remove_cvref_t<decltype(layer)>;
    return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
};

// Number of nodes in the subtree rooted here.
inline const auto size_algebra = [](const IntLayer<int>& layer) -> int {
    return 1 + (layer.left ? *layer.left : 0) + (layer.right ? *layer.right : 0);
};

// Height of the subtree rooted here.
inline const auto depth_algebra = [](const IntLayer<int>& layer) -> int {
    return 1 + std::max(layer.left ? *layer.left : 0, layer.right ? *layer.right : 0);
};

// Renders the annotation at every node, so a test can pin the whole
// decorated shape rather than only the root.
inline const auto render_annotations = [](const int& ann, const IntLayer<std::string>& layer) -> std::string {
    std::string left  = layer.left ? *layer.left : std::string{"."};
    std::string right = layer.right ? *layer.right : std::string{"."};
    return "(" + left + " " + std::to_string(ann) + " " + right + ")";
};

//         4
//     2       6
//   1   3
inline auto sample() -> Tree { return Tree::node(4, Tree::node(2, Tree::leaf(1), Tree::leaf(3)), Tree::leaf(6)); }

} // namespace

TEST_CASE("annotate decorates every node with its subtree fold", "[cofree]") {
    auto tree       = sample();
    auto annotated  = annotate<int, IntLayer>(size_algebra, fmap_fn, binary_tree_project, &tree);

    REQUIRE(annotation_of(annotated) == 5);

    // Every node, not just the root: the two leaves are 1, the inner
    // node covering {2, 1, 3} is 3, the root is 5.
    auto rendered = fold_cofree<std::string>(render_annotations, fmap_fn, annotated);
    REQUIRE(rendered == "(((. 1 .) 3 (. 1 .)) 5 (. 1 .))");
}

TEST_CASE("A different algebra decorates the same shape differently", "[cofree]") {
    auto tree      = sample();
    auto annotated = annotate<int, IntLayer>(depth_algebra, fmap_fn, binary_tree_project, &tree);

    REQUIRE(annotation_of(annotated) == 3);

    auto rendered = fold_cofree<std::string>(render_annotations, fmap_fn, annotated);
    REQUIRE(rendered == "(((. 1 .) 2 (. 1 .)) 3 (. 1 .))");
}

TEST_CASE("annotate runs the algebra once per node", "[cofree]") {
    // The point of annotating is that results are computed once and then
    // read, rather than recomputed on each query. A counting algebra
    // pins that: five nodes, five invocations.
    auto tree  = sample();
    int  calls = 0;

    auto counting = [&calls](const IntLayer<int>& layer) -> int {
        ++calls;
        return 1 + (layer.left ? *layer.left : 0) + (layer.right ? *layer.right : 0);
    };

    auto annotated = annotate<int, IntLayer>(counting, fmap_fn, binary_tree_project, &tree);

    REQUIRE(annotation_of(annotated) == 5);
    REQUIRE(calls == 5);

    // Reading the annotations back does not run the algebra again.
    auto again = fold_cofree<std::string>(render_annotations, fmap_fn, annotated);
    REQUIRE(calls == 5);
    REQUIRE(again == "(((. 1 .) 3 (. 1 .)) 5 (. 1 .))");
}

TEST_CASE("fold_cofree sees the annotation alongside children's results", "[cofree]") {
    // This is the course-of-values access: the algebra combines a value
    // computed on the earlier pass with results computed from below.
    auto tree      = sample();
    auto annotated = annotate<int, IntLayer>(size_algebra, fmap_fn, binary_tree_project, &tree);

    // Largest subtree size anywhere in the tree.
    auto largest = [](const int& ann, const IntLayer<int>& layer) -> int {
        int left  = layer.left ? *layer.left : 0;
        int right = layer.right ? *layer.right : 0;
        return std::max(ann, std::max(left, right));
    };
    REQUIRE(fold_cofree<int>(largest, fmap_fn, annotated) == 5);

    // Sum of all subtree sizes — a quantity that needs every node's
    // annotation, not just the root's.
    auto total = [](const int& ann, const IntLayer<int>& layer) -> int {
        int left  = layer.left ? *layer.left : 0;
        int right = layer.right ? *layer.right : 0;
        return ann + left + right;
    };
    REQUIRE(fold_cofree<int>(total, fmap_fn, annotated) == 5 + 3 + 1 + 1 + 1);
}

TEST_CASE("Cofree works over a second functor", "[cofree]") {
    // Nothing about Cofree is binary-tree-specific: RoseF holds its
    // children in a vector and needs no Box, and annotate is unchanged.
    auto tree = rose(1, {rose(2), rose(3, {rose(4)})});

    auto project = [](const RoseTreeFix<int>& t) -> const RoseLayer<int>::template F<RoseTreeFix<int> >& {
        return unwrap_fix(t);
    };

    auto rose_size = [](const RoseF<int, int>& layer) -> int {
        int total = 1;
        for (int child : layer.children) {
            total += child;
        }
        return total;
    };

    auto annotated = annotate<int, RoseLayer<int>::template F>(rose_size, fmap_fn, project, tree);

    REQUIRE(annotation_of(annotated) == 4);

    // Children's annotations: rose(2) is 1, rose(3, {rose(4)}) is 2.
    auto sizes = fold_cofree<std::vector<int> >(
        [](const int& ann, const RoseF<int, std::vector<int> >& layer) {
            std::vector<int> out{ann};
            for (const auto& child : layer.children) {
                out.insert(out.end(), child.begin(), child.end());
            }
            return out;
        },
        fmap_fn,
        annotated);

    REQUIRE(sizes == std::vector<int>{4, 1, 2, 1});
}

TEST_CASE("The annotated tree keeps the original shape", "[cofree]") {
    auto tree      = sample();
    auto annotated = annotate<int, IntLayer>(size_algebra, fmap_fn, binary_tree_project, &tree);

    // The values are still there underneath the annotations.
    auto values = [](const int&, const IntLayer<std::string>& layer) -> std::string {
        std::string left  = layer.left ? *layer.left : std::string{"."};
        std::string right = layer.right ? *layer.right : std::string{"."};
        return "(" + left + " " + std::to_string(layer.value) + " " + right + ")";
    };

    REQUIRE(fold_cofree<std::string>(values, fmap_fn, annotated) == "(((. 1 .) 2 (. 3 .)) 4 (. 6 .))");
}
