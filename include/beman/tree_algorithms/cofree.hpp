// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_COFREE_HPP
#define BEMAN_TREE_ALGORITHMS_COFREE_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/child_slot.hpp>
    #include <beman/tree_algorithms/recursion_schemes.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <functional>
        #include <type_traits>
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// The annotated tree, and why it is here.
//
// A fold collapses a tree to one answer. Plenty of useful questions want
// the answer *at every node* instead, and want later work to read those
// answers cheaply: the size of every subtree, the depth below every
// node, a hash per subtree for structural sharing, a memo table for a
// dynamic program over the tree. The recursion-scheme literature reaches
// for a course-of-values fold — histo, dyna, and the rest of the
// generalized family — and Decision 1 deliberately keeps all of that out
// of this repository.
//
// It stays out because it is not needed. Cofree<Ann, F> is the same tree
// shape as Fix<F> with one annotation added at every node, and annotate()
// below builds it with nothing but fold_with — no new verb, no
// distributive law, no generalized scheme. Fold once to decorate, then
// read the decorations as often as you like. fold_cofree() is the second
// half: a fold whose algebra sees the node's own annotation alongside
// its children's results, which is the access a course-of-values fold
// exists to provide.
//
// Cofree is a fixed point, so its recursive positions need the same
// treatment Fix<F> gets: a child_slot specialization putting a Box at the
// knot, where the type is incomplete inside its own layer. That
// specialization is declared below, before Cofree is defined, so any
// functor whose recursive positions route through child_slot_t works
// unchanged.

/** An annotated tree: the same shape as Fix<F>, with a value of type
 * @p Ann attached to every node.
 * @tparam Ann the annotation carried at each node
 * @tparam F unary template functor (takes the recursive position as its
 *           parameter), the same one Fix<F> would take
 */
template <typename Ann, template <typename> class F>
struct Cofree;

/** The knot, exactly as for Fix: inside its own layer Cofree is
 * incomplete, so the position must be an indirection. */
template <typename Ann, template <typename> class F>
struct child_slot<Cofree<Ann, F> > {
    using type = Box<Cofree<Ann, F> >;
};

template <typename Ann, template <typename> class F>
struct Cofree {
    Ann                annotation;
    F<Cofree<Ann, F> > children;
};

/** Read the annotation at the root of @p tree. */
template <typename Ann, template <typename> class F>
constexpr auto annotation_of(const Cofree<Ann, F>& tree) -> const Ann& {
    return tree.annotation;
}

/** Decorate every node of a tree with the fold result of the subtree
 * rooted there.
 *
 * Runs @p algebra exactly as fold_with would, but keeps every
 * intermediate result instead of discarding it: the carrier is
 * Cofree<Ann, F>, so each node retains both the answer computed for it
 * and the annotated children it was computed from. One pass, one
 * traversal, no new verb — the recursion is still fold_with's.
 *
 * @tparam Ann the annotation type; must be given explicitly
 * @tparam F   the layer functor, as for Fix<F>; must be given explicitly
 * @param algebra callable F<Ann> -> Ann, the same algebra a plain fold
 *                would take
 * @param fmap_fn callable (Fn, const F<A>&) -> F<B> — the layer's fmap
 * @param project callable Tree -> F<Handle>, one layer
 * @param tree    the tree (or child handle) to annotate
 */
template <typename Ann,
          template <typename>
          class F,
          typename Algebra,
          typename FMap,
          typename Project,
          typename Tree>
constexpr auto annotate(const Algebra& algebra, const FMap& fmap_fn, const Project& project, const Tree& tree)
    -> Cofree<Ann, F> {
    using Node = Cofree<Ann, F>;

    // The fold carrier is the annotated node itself. By the time this
    // runs on a layer, every child is already an annotated subtree, so
    // the annotation for this node is the algebra applied to the
    // children's annotations — and the layer is kept, not thrown away.
    auto step = [&](const F<Node>& layer) -> Node {
        auto child_annotations = fmap_fn([](const Node& child) -> Ann { return child.annotation; }, layer);
        return Node{algebra(child_annotations), layer};
    };

    return fold_with<Node>(step, fmap_fn, project, tree);
}

/** Fold an annotated tree, with the annotations visible to the algebra.
 *
 * The algebra receives the node's own annotation alongside its
 * children's already-folded results. That combination — a result
 * computed from below, plus a value computed on an earlier pass — is
 * what a course-of-values fold is for; here it needs only an ordinary
 * recursion over a tree that already carries its history.
 *
 * @tparam Result the fold carrier; must be given explicitly
 * @param algebra callable (const Ann&, F<Result>) -> Result
 * @param fmap_fn callable (Fn, const F<A>&) -> F<B> — the layer's fmap
 * @param tree    the annotated tree to fold
 */
template <typename Result, typename Ann, template <typename> class F, typename Algebra, typename FMap>
constexpr auto fold_cofree(const Algebra& algebra, const FMap& fmap_fn, const Cofree<Ann, F>& tree) -> Result {
    auto evaluated = fmap_fn(
        [&](const Cofree<Ann, F>& child) -> Result { return fold_cofree<Result>(algebra, fmap_fn, child); },
        tree.children);
    return algebra(tree.annotation, evaluated);
}

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_COFREE_HPP
