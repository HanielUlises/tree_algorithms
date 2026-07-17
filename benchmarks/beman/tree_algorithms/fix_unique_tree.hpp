// benchmarks/beman/tree_algorithms/fix_unique_tree.hpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef BEMAN_TREE_ALGORITHMS_BENCH_FIX_UNIQUE_TREE_HPP
#define BEMAN_TREE_ALGORITHMS_BENCH_FIX_UNIQUE_TREE_HPP

// A binary-tree base functor whose recursive child positions are managed by
// std::unique_ptr, not this library's Box.
//
// The point of the comparison: Fix<F> stores F<Fix<F>> and knows nothing
// about how F holds its children. Box, std::unique_ptr, std::shared_ptr,
// std::indirect, inline storage — that is the *functor's* choice (the
// user's), orthogonal to Fix. binary_tree.hpp's BinaryTreeF happens to pick
// Box (regular, deep-copy); UniquePtrTreeF here picks unique_ptr, which
// makes the resulting Fix tree move-only (irregular) and as cheap to build
// and fold as the hand-written unique_ptr tree. Same Fix, same recursion
// verbs; only the storage policy in the functor changes, and the runtime
// characteristics follow the storage policy, not Fix.

#include <beman/tree_algorithms/fix.hpp>
#include <beman/tree_algorithms/functor.hpp>

#include <functional>
#include <memory>
#include <type_traits>

namespace bench {

// One layer: a value plus unique_ptr-managed child positions. A null
// unique_ptr is an absent child (as a disengaged Box is for BinaryTreeF).
template <typename T, typename A>
struct UniquePtrTreeF {
    T                  value;
    std::unique_ptr<A> left;
    std::unique_ptr<A> right;
};

template <typename T>
struct UniquePtrTreeLayer {
    template <typename A>
    using F = UniquePtrTreeF<T, A>;
};

template <typename T>
using UniquePtrTreeFix = beman::tree_algorithms::Fix<UniquePtrTreeLayer<T>::template F>;

// Functor primitive: apply fn to each engaged child, left before right;
// absent (null) children stay absent, the value copies across.
template <typename T, typename A>
struct UniquePtrTreeFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const UniquePtrTreeF<T, A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        return UniquePtrTreeF<T, B>{
            layer.value,
            layer.left ? std::make_unique<B>(std::invoke(fn, *layer.left)) : std::unique_ptr<B>{},
            layer.right ? std::make_unique<B>(std::invoke(fn, *layer.right)) : std::unique_ptr<B>{},
        };
    }
};

template <typename T, typename A>
struct UniquePtrTreeFFunctorMap : beman::tree_algorithms::Functor<UniquePtrTreeFFunctorImpl<T, A> > {
    using UniquePtrTreeFFunctorImpl<T, A>::fmap;
};

} // namespace bench

namespace beman::tree_algorithms {

// Register the Functor instance so the lookup-based verbs (fold_fix,
// unfold_fix, refold) resolve fmap for UniquePtrTreeF exactly as they do for
// BinaryTreeF.
template <typename T, typename A>
inline constexpr auto functor_typeclass<bench::UniquePtrTreeF<T, A> > = bench::UniquePtrTreeFFunctorMap<T, A>{};

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_BENCH_FIX_UNIQUE_TREE_HPP
