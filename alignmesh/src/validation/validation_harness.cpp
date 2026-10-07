#include "alignmesh/validation/validation_harness.h"
#include "alignmesh/numerics/compensated_sum.h"
#include "alignmesh/numerics/seeded_rng.h"
#include "alignmesh/alignment/kabsch.h"
#include "alignmesh/geometry/rigid_transform.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>

namespace alignmesh::validation {

// ============================================================================
// Gauge R&R (ANOVA method, AIAG MSA 4th ed.)
// ============================================================================

GaugeRRResult compute_gauge_rr(
        const std::vector<MSAMeasurement>& measurements,
        double tolerance) {
    GaugeRRResult result;
    result.tolerance = tolerance;

    if (measurements.size() < 6) return result;

    // Determine structure: parts, operators, trials.
    std::set<int> parts_set, ops_set;
    std::map<std::pair<int,int>, std::vector<double>> cells;

    for (auto& m : measurements) {
        parts_set.insert(m.part_id);
        ops_set.insert(m.operator_id);
        cells[{m.part_id, m.operator_id}].push_back(m.value);
    }

    int p = static_cast<int>(parts_set.size());
    int o = static_cast<int>(ops_set.size());
    if (p < 2 || o < 1) return result;

    // Find number of trials (assume balanced).
    int r = 0;
    for (auto& [key, vals] : cells) {
        r = std::max(r, static_cast<int>(vals.size()));
    }
    if (r < 1) return result;

    result.n_parts = p;
    result.n_operators = o;
    result.n_trials = r;

    int N = static_cast<int>(measurements.size());

    // Grand mean.
    double grand_sum = 0;
    for (auto& m : measurements) grand_sum += m.value;
    double grand_mean = grand_sum / N;

    // Part means.
    std::map<int, double> part_mean;
    std::map<int, int> part_count;
    for (auto& m : measurements) {
        part_mean[m.part_id] += m.value;
        part_count[m.part_id]++;
    }
    for (auto& [pid, sum] : part_mean) sum /= part_count[pid];

    // Operator means.
    std::map<int, double> op_mean;
    std::map<int, int> op_count;
    for (auto& m : measurements) {
        op_mean[m.operator_id] += m.value;
        op_count[m.operator_id]++;
    }
    for (auto& [oid, sum] : op_mean) sum /= op_count[oid];

    // Sum of squares.
    double SS_parts = 0;
    for (auto& [pid, mean] : part_mean) {
        double d = mean - grand_mean;
        SS_parts += part_count[pid] * d * d;
    }

    double SS_operators = 0;
    for (auto& [oid, mean] : op_mean) {
        double d = mean - grand_mean;
        SS_operators += op_count[oid] * d * d;
    }

    double SS_total = 0;
    for (auto& m : measurements) {
        double d = m.value - grand_mean;
        SS_total += d * d;
    }

    // Within-cell (repeatability).
    double SS_within = 0;
    for (auto& [key, vals] : cells) {
        double cell_mean = 0;
        for (double v : vals) cell_mean += v;
        cell_mean /= static_cast<double>(vals.size());
        for (double v : vals) {
            double d = v - cell_mean;
            SS_within += d * d;
        }
    }

    // Mean squares.
    double df_parts = std::max(p - 1, 1);
    double df_ops = std::max(o - 1, 1);
    double df_within = std::max(N - p * o, 1);

    double MS_parts = SS_parts / df_parts;
    double MS_ops = SS_operators / df_ops;
    double MS_within = SS_within / df_within;

    // Variance components.
    double sigma2_repeat = MS_within;
    double sigma2_reprod = std::max((MS_ops - MS_within) / (p * r), 0.0);
    double sigma2_parts = std::max((MS_parts - MS_within) / (o * r), 0.0);

    result.ev = std::sqrt(sigma2_repeat);       // repeatability
    result.av = std::sqrt(sigma2_reprod);        // reproducibility
    result.grr = std::sqrt(sigma2_repeat + sigma2_reprod);
    result.pv = std::sqrt(sigma2_parts);
    result.tv = std::sqrt(sigma2_repeat + sigma2_reprod + sigma2_parts);

    if (result.tv > 0) {
        result.percent_ev = result.ev / result.tv * 100;
        result.percent_av = result.av / result.tv * 100;
        result.percent_grr = result.grr / result.tv * 100;
        result.percent_pv = result.pv / result.tv * 100;
    }

    if (tolerance > 0) {
        result.p_to_t = result.grr / tolerance * 100;
    }

    // Classification (AIAG MSA 4th ed.).
    if (result.percent_grr < 10.0)
        result.classification = "acceptable";
    else if (result.percent_grr < 30.0)
        result.classification = "marginal";
    else
        result.classification = "unacceptable";

    result.details.push_back("EV=" + std::to_string(result.ev));
    result.details.push_back("AV=" + std::to_string(result.av));
    result.details.push_back("GRR=" + std::to_string(result.grr));
    result.details.push_back("PV=" + std::to_string(result.pv));
    result.details.push_back("%GRR=" + std::to_string(result.percent_grr));

    result.computed = true;
    return result;
}

// ============================================================================
// Determinism manifest
// ============================================================================

DeterminismManifest generate_determinism_manifest() {
    DeterminismManifest m;
    m.machine_fingerprint = numerics::EnvironmentFingerprint::capture().to_string();

    // Compute a set of deterministic values from the core algorithms.
    // These exercise FP arithmetic across the system.

    // 1. Neumaier sum of harmonic series.
    {
        std::vector<double> h(1000);
        for (int i = 0; i < 1000; ++i) h[static_cast<std::size_t>(i)] = 1.0 / (i + 1);
        m.values.push_back({"harmonic_1000",
            numerics::neumaier_sum(h.begin(), h.end())});
    }

    // 2. Seeded RNG output.
    {
        numerics::SeededRng rng(42);
        double sum = 0;
        for (int i = 0; i < 100; ++i)
            sum += static_cast<double>(rng()) / static_cast<double>(numerics::SeededRng::max());
        m.values.push_back({"rng_sum_100", sum});
    }

    // 3. Kabsch alignment on known data.
    {
        Eigen::Matrix<double, 3, Eigen::Dynamic> src(3, 10), tgt(3, 10);
        numerics::SeededRng rng(123);
        std::uniform_real_distribution<double> d(-5, 5);
        for (int i = 0; i < 10; ++i) {
            src.col(i) = geometry::Vec3(d(rng), d(rng), d(rng));
        }
        auto R = Eigen::AngleAxisd(0.3, geometry::Vec3::UnitZ()).toRotationMatrix();
        auto T = geometry::RigidTransform::from_rotation_translation(R, geometry::Vec3(1, 2, 3));
        tgt = T.apply_cloud(src);

        auto result = alignment::align_landmarks(src, tgt);
        if (result.success) {
            m.values.push_back({"kabsch_rms", result.diagnostics.weighted_rms});
            m.values.push_back({"kabsch_R00", result.transform.matrix()(0, 0)});
        }
    }

    // 4. SE(3) exp/log round-trip.
    {
        geometry::Vec6 twist;
        twist << 0.1, 0.2, 0.3, 1.0, 2.0, 3.0;
        auto T = geometry::RigidTransform::exp(twist);
        auto twist2 = T.log();
        m.values.push_back({"se3_roundtrip_norm", (twist - twist2).norm()});
    }

    return m;
}

CrossMachineResult compare_manifests(
        const DeterminismManifest& a,
        const DeterminismManifest& b,
        double ulp_bound) {
    CrossMachineResult result;
    result.all_match = true;

    auto n = std::min(a.values.size(), b.values.size());
    for (std::size_t i = 0; i < n; ++i) {
        CrossMachineResult::ValueComparison vc;
        vc.name = a.values[i].first;
        vc.value_a = a.values[i].second;
        vc.value_b = b.values[i].second;
        vc.delta = std::abs(vc.value_a - vc.value_b);
        vc.ulp_bound = ulp_bound;

        if (ulp_bound > 0) {
            vc.matches = (vc.delta <= ulp_bound);
        } else {
            // Bit-exact comparison.
            vc.matches = (vc.value_a == vc.value_b);
        }

        if (!vc.matches) {
            result.all_match = false;
            result.divergences.push_back(
                vc.name + ": a=" + std::to_string(vc.value_a) +
                " b=" + std::to_string(vc.value_b) +
                " delta=" + std::to_string(vc.delta));
        }
        result.comparisons.push_back(std::move(vc));
    }

    if (a.values.size() != b.values.size()) {
        result.all_match = false;
        result.divergences.push_back("Manifest size mismatch: " +
            std::to_string(a.values.size()) + " vs " +
            std::to_string(b.values.size()));
    }

    return result;
}

} // namespace alignmesh::validation
