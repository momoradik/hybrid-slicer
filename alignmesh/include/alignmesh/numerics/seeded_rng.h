#pragma once

#include <cstdint>
#include <random>

namespace alignmesh::numerics {

// ============================================================================
// Deterministic seeded RNG
// ============================================================================
//
// Wraps std::mt19937_64 with a mandatory explicit seed. There is no default
// constructor — every RNG instance in this codebase MUST be traceable to a
// known seed for reproducibility.
//
// No code anywhere in this project may construct an unseeded RNG
// (e.g. std::mt19937{std::random_device{}()}). If you need randomness,
// create a SeededRng with a documented seed and pass it by reference.
//
// Satisfies UniformRandomBitGenerator, so it works with all standard
// distributions (std::uniform_real_distribution, std::normal_distribution,
// etc.).
// ============================================================================

class SeededRng {
public:
    using result_type = std::mt19937_64::result_type;

    /// Construct with an explicit seed. This is the ONLY way to create one.
    explicit SeededRng(std::uint64_t seed) : engine_(seed) {}

    /// No default construction — every RNG must be explicitly seeded.
    SeededRng() = delete;

    SeededRng(const SeededRng&)            = default;
    SeededRng& operator=(const SeededRng&) = default;
    SeededRng(SeededRng&&)                 noexcept = default;
    SeededRng& operator=(SeededRng&&)      noexcept = default;

    /// Generate the next value (UniformRandomBitGenerator requirement).
    result_type operator()() { return engine_(); }

    static constexpr result_type min() { return std::mt19937_64::min(); }
    static constexpr result_type max() { return std::mt19937_64::max(); }

    /// Re-seed (deterministic reset to a known state).
    void seed(std::uint64_t s) { engine_.seed(s); }

    /// Advance the state by n steps without producing output.
    void discard(unsigned long long n) { engine_.discard(n); }

private:
    std::mt19937_64 engine_;
};

} // namespace alignmesh::numerics
