#pragma once

#include <cmath>
#include <cstddef>
#include <iterator>
#include <span>
#include <type_traits>

namespace alignmesh::numerics {

// ============================================================================
// Neumaier (improved Kahan) compensated summation
// ============================================================================
//
// Tracks a running compensation term so that rounding errors from each
// addition are recaptured. Produces a result that is nearly fully accurate
// regardless of input ordering or catastrophic cancellation.
//
// ALL reductions in this codebase MUST use one of these functions instead of
// raw std::accumulate, manual += loops, or parallel reductions, which do not
// guarantee a deterministic evaluation order or compensated accuracy.
// ============================================================================

/// Sum a range of floating-point values with Neumaier compensation.
/// Requires at least InputIterator.
template <typename InputIt>
auto neumaier_sum(InputIt first, InputIt last)
    -> typename std::iterator_traits<InputIt>::value_type
{
    using T = typename std::iterator_traits<InputIt>::value_type;
    static_assert(std::is_floating_point_v<T>,
                  "neumaier_sum requires floating-point value type");

    T sum          = T{0};
    T compensation = T{0};

    for (; first != last; ++first) {
        const T val = *first;
        const T t   = sum + val;

        // Compensate the smaller operand's rounding loss.
        if (std::abs(sum) >= std::abs(val)) {
            compensation += (sum - t) + val;
        } else {
            compensation += (val - t) + sum;
        }
        sum = t;
    }

    return sum + compensation;
}

/// Convenience overload for std::span.
template <typename T>
T neumaier_sum(std::span<const T> values) {
    return neumaier_sum(values.begin(), values.end());
}

// ============================================================================
// Fixed-order pairwise summation
// ============================================================================
//
// Recursively bisects the input at fixed midpoints, applying Neumaier
// compensation at the leaf blocks. This guarantees:
//
//   1. DETERMINISTIC evaluation order — the binary tree structure is the same
//      for the same input length, regardless of thread count or hardware.
//   2. O(log n) error growth — far better than the O(n) of naive serial sum.
//   3. High leaf accuracy — Neumaier handles each small block.
//
// Use this for large reductions where both determinism and accuracy matter.
// ============================================================================

/// Leaf-block size for pairwise summation. Below this threshold the
/// range is summed directly with Neumaier compensation.
inline constexpr std::size_t kPairwiseThreshold = 128;

/// Pairwise-sum a range of floating-point values.
/// Requires RandomAccessIterator (must be able to split at midpoint).
template <typename RandomIt>
auto pairwise_sum(RandomIt first, RandomIt last)
    -> typename std::iterator_traits<RandomIt>::value_type
{
    using T = typename std::iterator_traits<RandomIt>::value_type;
    static_assert(std::is_floating_point_v<T>,
                  "pairwise_sum requires floating-point value type");
    static_assert(
        std::is_base_of_v<std::random_access_iterator_tag,
                          typename std::iterator_traits<RandomIt>::iterator_category>,
        "pairwise_sum requires random-access iterators");

    const auto n = std::distance(first, last);
    if (n <= 0) return T{0};

    // Leaf: use Neumaier for maximum accuracy on the small block.
    if (static_cast<std::size_t>(n) <= kPairwiseThreshold) {
        return neumaier_sum(first, last);
    }

    // Fixed midpoint — deterministic split regardless of runtime state.
    const auto mid = first + n / 2;
    return pairwise_sum(first, mid) + pairwise_sum(mid, last);
}

/// Convenience overload for std::span.
template <typename T>
T pairwise_sum(std::span<const T> values) {
    return pairwise_sum(values.begin(), values.end());
}

} // namespace alignmesh::numerics
