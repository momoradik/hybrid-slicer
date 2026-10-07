// repro_manifest — deterministic numeric output for reproducibility testing.
//
// Exercises every numerics primitive and prints results in hex-exact format
// (hexfloat for doubles, zero-padded hex for RNG integers). The CI script
// runs this binary twice and asserts the two outputs are byte-identical.
//
// If this binary ever produces different output between two runs on the
// same machine with the same binary, something is broken in the
// determinism guarantees.

#include "alignmesh/numerics/compensated_sum.h"
#include "alignmesh/numerics/seeded_rng.h"
#include "alignmesh/numerics/environment_fingerprint.h"
#include "alignmesh/geometry/rigid_transform.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <random>
#include <span>
#include <vector>

using namespace alignmesh::numerics;
using namespace alignmesh::geometry;

int main() {
    std::cout << std::hexfloat;

    // --- Section 1: SeededRng raw output (seed 42, first 20 values) ----------
    {
        std::cout << "# rng_seed_42\n";
        SeededRng rng(42);
        for (int i = 0; i < 20; ++i) {
            std::cout << std::hex << std::setfill('0') << std::setw(16)
                      << rng() << '\n';
        }
    }

    // --- Section 2: neumaier_sum of 10000 RNG-generated doubles --------------
    {
        std::cout << "# neumaier_sum_10k\n";
        SeededRng rng(12345);
        std::vector<double> vals(10000);
        for (auto& v : vals) {
            v = static_cast<double>(rng()) /
                static_cast<double>(SeededRng::max()) * 2.0 - 1.0;
        }
        std::cout << neumaier_sum(std::span<const double>(vals)) << '\n';
    }

    // --- Section 3: pairwise_sum of same 10000 doubles -----------------------
    {
        std::cout << "# pairwise_sum_10k\n";
        SeededRng rng(12345);  // same seed → same sequence
        std::vector<double> vals(10000);
        for (auto& v : vals) {
            v = static_cast<double>(rng()) /
                static_cast<double>(SeededRng::max()) * 2.0 - 1.0;
        }
        std::cout << pairwise_sum(std::span<const double>(vals)) << '\n';
    }

    // --- Section 4: catastrophic cancellation --------------------------------
    {
        std::cout << "# cancellation\n";
        std::vector<double> v = {1.0, 1e100, 1.0, -1e100};
        std::cout << neumaier_sum(std::span<const double>(v)) << '\n';
        std::cout << pairwise_sum(std::span<const double>(v)) << '\n';
    }

    // --- Section 5: harmonic partial sum (order-sensitive) --------------------
    {
        std::cout << "# harmonic_5000\n";
        constexpr int n = 5000;
        std::vector<double> harmonic(n);
        for (int i = 0; i < n; ++i) {
            harmonic[static_cast<std::size_t>(i)] =
                1.0 / static_cast<double>(i + 1);
        }
        std::cout << neumaier_sum(std::span<const double>(harmonic)) << '\n';
        std::cout << pairwise_sum(std::span<const double>(harmonic)) << '\n';
    }

    // --- Section 6: RNG-driven distribution outputs --------------------------
    {
        std::cout << "# uniform_dist_seed_7\n";
        SeededRng rng(7);
        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        for (int i = 0; i < 50; ++i) {
            std::cout << dist(rng) << '\n';
        }
    }

    // --- Section 7: RigidTransform compose + inverse -------------------------
    {
        std::cout << "# rigid_transform_compose_inverse\n";
        Mat3 R;
        R << 0, -1, 0,
             1,  0, 0,
             0,  0, 1;
        Vec3 t(1.0, 2.0, 3.0);
        auto T = RigidTransform::from_rotation_translation(R, t);
        auto product = T * T.inverse();
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                std::cout << product.matrix()(i, j) << '\n';
    }

    // --- Section 8: SE(3) exp/log round-trip ----------------------------------
    {
        std::cout << "# se3_exp_log\n";
        SeededRng rng(314);
        std::uniform_real_distribution<double> angle(-std::numbers::pi, std::numbers::pi);
        std::uniform_real_distribution<double> trans(-10.0, 10.0);
        std::normal_distribution<double> gauss(0.0, 1.0);

        for (int k = 0; k < 20; ++k) {
            Vec3 axis(gauss(rng), gauss(rng), gauss(rng));
            double n = axis.norm();
            if (n < 1e-12) axis = Vec3::UnitX();
            else axis /= n;
            Mat3 R = Eigen::AngleAxisd(angle(rng), axis).toRotationMatrix();
            Vec3 tv(trans(rng), trans(rng), trans(rng));
            auto T = RigidTransform::from_rotation_translation(R, tv);
            auto twist = T.log();
            auto T2 = RigidTransform::exp(twist);
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    std::cout << T2.matrix()(i, j) << '\n';
        }
    }

    // --- Section 9: environment fingerprint ----------------------------------
    {
        std::cout << "# fingerprint\n";
        std::cout << EnvironmentFingerprint::capture().to_string();
    }

    return 0;
}
