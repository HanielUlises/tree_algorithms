# Arena layout and Cofree annotations

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

Two additions, both aimed at objections the paper invites but does not
yet answer with code. Neither adds a verb; both sit at the tier
`fold_map` occupies — algorithms derived from the existing three.

## The arena tree — answering the layout objection

Before this, every shipped representation spent a pointer per edge:
`shared_ptr` in `BinaryTree` and `FringeTree`, `unique_ptr` in the nonce
tree, a `Box` at the knot in `Fix` itself. That invites a reasonable
reading of the whole proposal — that these verbs presuppose a linked
structure and have nothing to say about the layout a performance-minded
implementer would actually ship.

`ArenaTree<T>` (`include/beman/tree_algorithms/arena_tree.hpp`) is that
layout: nodes contiguous in one `std::vector`, children as `uint32_t`
offsets, the absent child a sentinel index rather than a null pointer.
Nothing in the algorithms changed to accommodate it. The projection
hands back cursors instead of pointers; that is the entire difference.
`examples/arena_tree_layout.cpp` folds one shape over both layouts and
shows depth, in-order flattening and elementwise sum agreeing.

### The honest cost

Folding *out* of an arena is free of the linked forms' overhead, and
that is the direction the layout argument depends on. Building *into*
one through `unfold_with` is not. A linked embedding joins two subtrees
by storing two pointers, in constant time; a flat arena has no such
move, because the children's nodes must end up contiguous with the
parent's, so joining copies both child arenas and rebases their indices.
Bottom-up construction through `arena_tree_embed` is therefore quadratic
in the node count.

This is a real limitation and the header says so rather than burying it.
For a large tree, build into a linked form and flatten once, or fill the
arena top-down with an explicit builder. Whether the paper wants to
present a top-down arena builder as well is an open question — it would
make `unfold_with` into an arena practical, at the cost of a second
embedding concept the current design does not have.

## Cofree — course-of-values access without the schemes

Decision 1 keeps `histo`, `dyna`, `chrono` and the rest of the
generalized family out of this repository, on the grounds that they are
a separate campaign. The obvious question is what a user does when they
genuinely need what those schemes provide: an answer at every node,
readable cheaply by later work.

`Cofree<Ann, F>` (`include/beman/tree_algorithms/cofree.hpp`) is `Fix<F>`'s
shape with one annotation per node. `annotate()` builds it with nothing
but `fold_with` — the carrier is the annotated node itself, so the pass
that computes each answer keeps it rather than discarding it.
`fold_cofree()` then folds with the annotations visible to the algebra,
which is precisely the access a course-of-values fold exists to give.

The strength of this as a paper argument is that it makes Decision 1
defensible rather than merely stated: the exotic schemes stay out
*because they are not needed*, and there is now runnable evidence.
`examples/cofree_annotations.cpp` does CSE detection over an expression
tree in two linear passes. The test suite pins the claim that matters —
five nodes, five algebra invocations, and reading the annotations back
adds none.

### Where it does not reach

`Cofree` is a fixed point, so its recursive positions need the same
`child_slot` treatment `Fix` gets, and the specialization putting a `Box`
at its knot is declared in `cofree.hpp`. The consequence is that
`annotate` works for any functor whose recursive positions route through
`child_slot_t` (`BinaryTreeF`, `ExprF`) or through a container tolerating
incomplete types (`RoseF`), and not for layers holding `A` directly by
value (`ArenaTreeF`, `FringeBranch`). Those layers were never usable as
fixed points in the first place — they exist to carry cursors and fold
results — so nothing is lost, but the asymmetry is worth stating before
someone reports it as a bug.

## Open questions

- Should `annotate`/`fold_cofree` appear in the paper's synopsis, or stay
  implementation evidence? They are derived algorithms, like `fold_map`,
  which the 2026-07-14 amendment did bring into scope.
- Does the arena tree belong in the benchmarks alongside `naive_trees.hpp`?
  The cache-behaviour comparison is the interesting half and is not
  measured yet.
- A top-down arena builder would remove the quadratic embedding, but
  needs a concept the design does not currently have.
