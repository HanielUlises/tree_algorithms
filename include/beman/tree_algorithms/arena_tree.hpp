// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_ARENA_TREE_HPP
#define BEMAN_TREE_ALGORITHMS_ARENA_TREE_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/functor.hpp>
    #include <beman/tree_algorithms/overloaded.hpp>

    // <cassert> is macro-only and therefore not importable; it is included
    // unconditionally so assert works in the module build as well.
    #include <cassert>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <cstddef>
        #include <cstdint>
        #include <functional>
        #include <type_traits>
        #include <utility>
        #include <variant>
        #include <vector>
    #endif

namespace beman::tree_algorithms {

// A tree with no pointers in it at all.
//
// Every representation shipped so far spends a pointer per edge:
// BinaryTree (binary_tree.hpp) chases shared_ptr, the nonce tree chases
// unique_ptr, FringeTree (fringe_tree.hpp) chases shared_ptr, and Fix
// itself chases a Box at the knot. That invites the obvious objection to
// the whole family — that these verbs presuppose a linked structure, and
// have nothing to say about the layout an implementer would actually
// ship: one flat array of nodes, children named by index, no allocation
// per node and no pointer chasing on traversal.
//
// ArenaTree<T> below is that layout. Nodes live contiguously in a single
// std::vector; a child is a std::uint32_t offset into it; the empty
// child is a sentinel index rather than a null pointer. Nothing about
// the algorithms changes to accommodate it. The projection hands back
// cursors instead of pointers, and that is the entire difference — the
// same fold_with, the same fold_map, the same algebras.
//
// The embedding is where this representation stops being free, and the
// header does not hide it: see ArenaTreeEmbedFn below.

// ---------------------------------------------------------------------
// The representation.
// ---------------------------------------------------------------------

/** Binary tree stored as a flat arena: nodes in one contiguous vector,
 * children addressed by index rather than by pointer. Values live at
 * every node. Copies are one vector copy; traversal touches no
 * allocation and follows no pointers.
 * @tparam T element type stored at each node
 */
template <class T>
class ArenaTree {
  public:
    using value_type = T;
    using index_type = std::uint32_t;

    /** Sentinel standing in for an absent child (the null of this
     * representation). */
    static constexpr index_type npos = static_cast<index_type>(-1);

    /** One node of the arena: a value and two child indices. */
    struct Node {
        T          d_value;
        index_type d_left;
        index_type d_right;
    };

    /** Construct the empty tree (no nodes). */
    static auto empty() -> ArenaTree { return ArenaTree{}; }

    /** Construct a single-node tree holding @p value. */
    static auto leaf(T value) -> ArenaTree {
        ArenaTree tree;
        tree.d_nodes.push_back(Node{std::move(value), npos, npos});
        tree.d_root = 0U;
        return tree;
    }

    /** Construct a node holding @p value over two subtrees. The
     * subtrees' arenas are copied into the new one and their indices
     * rebased; see ArenaTreeEmbedFn for why that cost is inherent to
     * building a flat arena bottom-up. */
    static auto node(T value, ArenaTree left, ArenaTree right) -> ArenaTree {
        ArenaTree tree;
        auto      left_root  = tree.absorb(left);
        auto      right_root = tree.absorb(right);
        tree.d_nodes.push_back(Node{std::move(value), left_root, right_root});
        tree.d_root = static_cast<index_type>(tree.d_nodes.size() - 1U);
        return tree;
    }

    /** True when the tree holds no nodes. */
    auto is_empty() const -> bool { return d_root == npos; }

    /** Index of the root node, or npos when the tree is empty. */
    auto root() const -> index_type { return d_root; }

    /** Number of nodes held in the arena. */
    auto size() const -> std::size_t { return d_nodes.size(); }

    /** Access one node by index; precondition: @p index is not npos and
     * is in range. */
    auto at(index_type index) const -> const Node& {
        assert(index != npos);
        assert(static_cast<std::size_t>(index) < d_nodes.size());
        return d_nodes[index];
    }

    /** The backing storage, exposed so callers can observe that the
     * nodes really are contiguous. */
    auto nodes() const -> const std::vector<Node>& { return d_nodes; }

  private:
    std::vector<Node> d_nodes;
    index_type        d_root = npos;

    /** Copy @p other's nodes onto the end of this arena, rebasing every
     * child index by the insertion offset, and return @p other's root in
     * this arena's numbering. */
    auto absorb(const ArenaTree& other) -> index_type {
        if (other.is_empty()) {
            return npos;
        }
        auto offset = static_cast<index_type>(d_nodes.size());
        for (const auto& node : other.d_nodes) {
            auto rebase = [offset](index_type child) { return child == npos ? npos : child + offset; };
            d_nodes.push_back(Node{node.d_value, rebase(node.d_left), rebase(node.d_right)});
        }
        return other.d_root + offset;
    }
};

/** A position in an arena: the tree, plus the index of one node in it.
 * This is the handle the direct verbs recurse on — the analogue of the
 * raw child pointer the linked representations hand back, and the same
 * size as one. */
template <typename T>
struct ArenaCursor {
    const ArenaTree<T>*               tree;
    typename ArenaTree<T>::index_type index;
};

/** The cursor at the root of @p tree; the starting point for a fold. */
template <typename T>
constexpr auto arena_root(const ArenaTree<T>& tree) -> ArenaCursor<T> {
    return ArenaCursor<T>{&tree, tree.root()};
}

// ---------------------------------------------------------------------
// The layer.
// ---------------------------------------------------------------------

/** One layer of an arena tree with the recursive positions abstracted:
 * either absent, or a value over two child handles.
 * @tparam T element type
 * @tparam A recursive position handle type
 */
struct ArenaAbsent {};

template <typename T, typename A>
struct ArenaNode {
    T value;
    A left;
    A right;
};

template <typename T, typename A>
using ArenaTreeF = std::variant<ArenaAbsent, ArenaNode<T, A> >;

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

/** Functor primitive for ArenaTreeF<T, A>: applies @p fn to each child
 * handle, left before right; absent layers pass through. */
template <typename T, typename A>
struct ArenaTreeFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const ArenaTreeF<T, A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        return std::visit(overloaded{
                              [](const ArenaAbsent&) -> ArenaTreeF<T, B> { return ArenaAbsent{}; },
                              [&fn](const ArenaNode<T, A>& n) -> ArenaTreeF<T, B> {
                                  auto left = std::invoke(fn, n.left);
                                  return ArenaNode<T, B>{n.value, std::move(left), std::invoke(fn, n.right)};
                              },
                          },
                          layer);
    }
};

/** Functor map for ArenaTreeF<T, A>: the fmap primitive plus the derived
 * operations from the Functor CRTP base. */
template <typename T, typename A>
struct ArenaTreeFFunctorMap : Functor<ArenaTreeFFunctorImpl<T, A> > {
    using ArenaTreeFFunctorImpl<T, A>::fmap;
};

/** Registers ArenaTreeFFunctorMap as the Functor instance for
 * ArenaTreeF<T, A>. */
template <typename T, typename A>
inline constexpr auto functor_typeclass<ArenaTreeF<T, A> > = ArenaTreeFFunctorMap<T, A>{};

// ---------------------------------------------------------------------
// Projection and embedding: the direct verbs' two ingredients.
// ---------------------------------------------------------------------

/** Projection for fold_with: exposes one layer of an arena tree,
 * children as cursors into the arena we already have. Nothing is copied
 * beyond the one value in the layer, and no allocation happens — the
 * child handles are two words each, resolved by indexing rather than by
 * dereference.
 *
 * Two overloads, for the two ways a fold enters. The cursor overload is
 * what the recursion itself calls, because the handles inside a
 * projected layer are cursors by value. The pointer overload exists
 * because the typeclass-lookup fold_map bootstraps by taking the
 * address of whatever it was handed; it simply forwards. */
struct ArenaTreeProjectFn {
    template <typename T>
    auto operator()(ArenaCursor<T> cursor) const -> ArenaTreeF<T, ArenaCursor<T> > {
        if (cursor.index == ArenaTree<T>::npos) {
            return ArenaAbsent{};
        }
        const auto& node = cursor.tree->at(cursor.index);
        return ArenaNode<T, ArenaCursor<T> >{node.d_value,
                                            ArenaCursor<T>{cursor.tree, node.d_left},
                                            ArenaCursor<T>{cursor.tree, node.d_right}};
    }

    template <typename T>
    auto operator()(const ArenaCursor<T>* cursor) const -> ArenaTreeF<T, ArenaCursor<T> > {
        return (*this)(*cursor);
    }
};

inline constexpr ArenaTreeProjectFn arena_tree_project{};

/** Embedding for unfold_with: rebuilds one layer in the arena's own
 * representation.
 *
 * This is the one place the flat layout costs more than a linked one,
 * and it is worth being plain about. A linked embedding joins two
 * subtrees by storing two pointers, in constant time. A flat arena has
 * no such move: the children's nodes must end up contiguous with the
 * parent's, so joining copies both child arenas and rebases their
 * indices. Building bottom-up through this embedding is therefore
 * quadratic in the node count, and unfold_with into an ArenaTree is the
 * wrong tool for a large tree — build into a linked form and flatten
 * once, or fill the arena top-down with an explicit builder. Folding
 * *out* of an arena, which is the direction that matters for the layout
 * argument, carries none of this cost. */
struct ArenaTreeEmbedFn {
    template <typename T>
    auto operator()(ArenaTreeF<T, ArenaTree<T> >&& layer) const -> ArenaTree<T> {
        return std::visit(overloaded{
                              [](ArenaAbsent&&) { return ArenaTree<T>::empty(); },
                              [](ArenaNode<T, ArenaTree<T> >&& n) {
                                  return ArenaTree<T>::node(std::move(n.value), std::move(n.left), std::move(n.right));
                              },
                          },
                          std::move(layer));
    }
};

inline constexpr ArenaTreeEmbedFn arena_tree_embed{};

// ---------------------------------------------------------------------
// Elementwise layer fold (for fold_map).
// ---------------------------------------------------------------------

/** Folds one ArenaTreeF layer elementwise, in order: the left child's
 * already-folded result, then the mapped node value, then the right
 * child's result. Absent layers contribute the identity. In-order
 * traversal is this representation's contract, matching BinaryTree; a
 * non-commutative combine observes it. */
struct ArenaTreeLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result, typename T>
    constexpr auto operator()(const MapFn&                 map_fn,
                              const Combine&               combine,
                              const Result&                identity,
                              const ArenaTreeF<T, Result>& layer) const -> Result {
        return std::visit(overloaded{
                              [&](const ArenaAbsent&) -> Result { return identity; },
                              [&](const ArenaNode<T, Result>& n) -> Result {
                                  return combine(combine(n.left, map_fn(n.value)), n.right);
                              },
                          },
                          layer);
    }
};

inline constexpr ArenaTreeLayerFoldMap arena_tree_layer_fold_map{};

/** Lookup registrations: the projection keyed on the cursor, which is
 * this representation's tree handle, and the layer fold keyed on the
 * layer type. */
template <typename T>
inline constexpr auto project_typeclass<ArenaCursor<T> > = arena_tree_project;

template <typename T, typename A>
inline constexpr auto layer_fold_typeclass<ArenaTreeF<T, A> > = arena_tree_layer_fold_map;

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_ARENA_TREE_HPP
