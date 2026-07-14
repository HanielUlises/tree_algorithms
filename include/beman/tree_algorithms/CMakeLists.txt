# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    target_sources(
        beman.tree_algorithms
        PUBLIC
            FILE_SET CXX_MODULES FILES tree_algorithms.cppm
            FILE_SET HEADERS
                FILES
                    binary_tree.hpp
                    box.hpp
                    config.hpp
                    expression.hpp
                    fix.hpp
                    fold_map.hpp
                    fringe_tree.hpp
                    functor.hpp
                    functors.hpp
                    overloaded.hpp
                    recursion_schemes.hpp
                    recursion_schemes_lookup.hpp
                    tree_algorithms.hpp
                    todo.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/tree_algorithms/config_generated.hpp"
    )
else()
    target_sources(
        beman.tree_algorithms
        PUBLIC
            FILE_SET HEADERS
                FILES
                    binary_tree.hpp
                    box.hpp
                    config.hpp
                    expression.hpp
                    fix.hpp
                    fold_map.hpp
                    fringe_tree.hpp
                    functor.hpp
                    functors.hpp
                    overloaded.hpp
                    recursion_schemes.hpp
                    recursion_schemes_lookup.hpp
                    tree_algorithms.hpp
                    todo.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/tree_algorithms/config_generated.hpp"
    )
endif()
