#include "test_harness_gt.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <cmath>
#include <numbers>
#include <random>
#include <algorithm>

namespace alignmesh::test_harness {

static constexpr double kPi = std::numbers::pi;

// ── Transform helpers ────────────────────────────────────────────────────

geometry::TriangleMesh apply_known_transform(
        const geometry::TriangleMesh& mesh,
        const geometry::RigidTransform& T) {
    return mesh.transformed(T);
}

Eigen::Matrix<double, 3, Eigen::Dynamic> apply_known_transform(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        const geometry::RigidTransform& T) {
    return T.apply_cloud(points);
}

// ── Synthetic scan generation ────────────────────────────────────────────

SyntheticScan make_synthetic_scan(
        const geometry::TriangleMesh& reference,
        const geometry::RigidTransform& T_gt,
        const ScanParams& params) {
    SyntheticScan scan;
    numerics::SeededRng rng(params.seed);
    std::uniform_real_distribution<double> uniform01(0.0, 1.0);
    std::normal_distribution<double> noise(0.0, params.noise_sigma);
    std::uniform_real_distribution<double> flyer(-params.outlier_range, params.outlier_range);

    const auto& V = reference.vertices();
    const auto& F = reference.triangles();
    Eigen::Index nf = F.cols();

    // Compute per-triangle area for area-weighted sampling.
    std::vector<double> areas(static_cast<std::size_t>(nf));
    double total_area = 0;
    for (Eigen::Index f = 0; f < nf; ++f) {
        Eigen::Vector3d v0 = V.col(F(0, f));
        Eigen::Vector3d v1 = V.col(F(1, f));
        Eigen::Vector3d v2 = V.col(F(2, f));
        double a = 0.5 * (v1 - v0).cross(v2 - v0).norm();
        areas[static_cast<std::size_t>(f)] = a;
        total_area += a;
    }

    // Target number of points.
    int n_target = std::max(10, static_cast<int>(nf * params.resample_density));
    int n_outliers = static_cast<int>(n_target * params.outlier_fraction);
    int n_inliers = n_target - n_outliers;

    // Build CDF for area-weighted triangle selection.
    std::vector<double> cdf(static_cast<std::size_t>(nf));
    if (total_area > 0) {
        cdf[0] = areas[0] / total_area;
        for (std::size_t i = 1; i < cdf.size(); ++i)
            cdf[i] = cdf[i - 1] + areas[i] / total_area;
    } else {
        // Degenerate mesh — uniform.
        for (std::size_t i = 0; i < cdf.size(); ++i)
            cdf[i] = static_cast<double>(i + 1) / static_cast<double>(nf);
    }

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, n_target);

    // Sample inlier points via barycentric coordinates on random triangles.
    for (int i = 0; i < n_inliers; ++i) {
        double r = uniform01(rng);
        auto it = std::lower_bound(cdf.begin(), cdf.end(), r);
        auto fi = static_cast<Eigen::Index>(std::distance(cdf.begin(), it));
        if (fi >= nf) fi = nf - 1;

        // Random barycentric coordinates.
        double u = uniform01(rng);
        double v = uniform01(rng);
        if (u + v > 1.0) { u = 1.0 - u; v = 1.0 - v; }
        double w = 1.0 - u - v;

        Eigen::Vector3d p = w * V.col(F(0, fi)) + u * V.col(F(1, fi)) + v * V.col(F(2, fi));

        // Add Gaussian noise.
        p.x() += noise(rng);
        p.y() += noise(rng);
        p.z() += noise(rng);

        pts.col(i) = p;
    }

    // Inject outlier/flyer points.
    Eigen::Vector3d bbox_center = (V.rowwise().maxCoeff() + V.rowwise().minCoeff()) / 2.0;
    for (int i = n_inliers; i < n_target; ++i) {
        pts.col(i) = bbox_center + Eigen::Vector3d(flyer(rng), flyer(rng), flyer(rng));
    }

    // Points are currently in the REFERENCE frame. Transform to raw measured frame.
    auto T_inv = T_gt.inverse();
    scan.points = T_inv.apply_cloud(pts);
    scan.n_inliers = n_inliers;
    scan.n_outliers = n_outliers;

    return scan;
}

// ── Test geometries ──────────────────────────────────────────────────────

geometry::TriangleMesh make_l_bracket_with_fillet(
        double arm_length, double arm_width, double thickness,
        double fillet_radius, int angular_segments, int linear_segments) {

    // L-bracket: two arms meeting at origin, extending along +X and +Y.
    // Arm 1: [0, arm_length] x [0, arm_width] x [0, thickness]
    // Arm 2: [0, arm_width] x [0, arm_length] x [0, thickness]
    // Fillet: quarter-cylinder at the inner corner.

    std::vector<Eigen::Vector3d> verts;
    std::vector<Eigen::Vector3i> tris;

    auto add_quad = [&](int a, int b, int c, int d) {
        tris.push_back(Eigen::Vector3i(a, b, c));
        tris.push_back(Eigen::Vector3i(a, c, d));
    };

    // Helper: create a flat rectangular face at given z, with nx x ny divisions.
    auto make_rect = [&](double x0, double y0, double x1, double y1, double z,
                         int nx, int ny, bool flip) -> int {
        int base = static_cast<int>(verts.size());
        for (int iy = 0; iy <= ny; ++iy) {
            double y = y0 + (y1 - y0) * iy / ny;
            for (int ix = 0; ix <= nx; ++ix) {
                double x = x0 + (x1 - x0) * ix / nx;
                verts.push_back(Eigen::Vector3d(x, y, z));
            }
        }
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                int i00 = base + iy * (nx + 1) + ix;
                int i10 = i00 + 1;
                int i01 = i00 + (nx + 1);
                int i11 = i01 + 1;
                if (flip) {
                    add_quad(i00, i01, i11, i10);
                } else {
                    add_quad(i00, i10, i11, i01);
                }
            }
        }
        return base;
    };

    int n = linear_segments;

    // Top face of arm 1 (z = thickness).
    make_rect(arm_width, 0, arm_length, arm_width, thickness, n, n, false);
    // Top face of arm 2.
    make_rect(0, arm_width, arm_width, arm_length, thickness, n, n, false);
    // Top face of corner block.
    make_rect(0, 0, arm_width, arm_width, thickness, n, n, false);

    // Bottom faces (z = 0).
    make_rect(arm_width, 0, arm_length, arm_width, 0, n, n, true);
    make_rect(0, arm_width, arm_width, arm_length, 0, n, n, true);
    make_rect(0, 0, arm_width, arm_width, 0, n, n, true);

    // Side walls (outer edges).
    make_rect(0, 0, arm_length, 0, 0, n, 1, false);  // placeholder — will use actual z coords
    // For simplicity, add outer walls as quads at the extremes.
    // Arm 1 outer walls.
    for (int iz = 0; iz < 1; ++iz) {
        // +Y wall of arm 1.
        make_rect(arm_width, arm_width, arm_length, arm_width, 0, n, 1, false);
    }

    // Fillet: quarter-cylinder from (arm_width, arm_width) going inward.
    // The fillet connects the inner walls of the two arms.
    {
        double cx = arm_width - fillet_radius;
        double cy = arm_width - fillet_radius;
        int base = static_cast<int>(verts.size());

        // Generate fillet surface vertices.
        for (int iz = 0; iz <= 1; ++iz) {
            double z = iz * thickness;
            for (int ia = 0; ia <= angular_segments; ++ia) {
                double angle = kPi / 2.0 * ia / angular_segments;
                double x = cx + fillet_radius * std::cos(angle);
                double y = cy + fillet_radius * std::sin(angle);
                verts.push_back(Eigen::Vector3d(x, y, z));
            }
        }

        // Triangulate the fillet strip.
        int ring = angular_segments + 1;
        for (int ia = 0; ia < angular_segments; ++ia) {
            int i0 = base + ia;
            int i1 = base + ia + 1;
            int i2 = base + ring + ia;
            int i3 = base + ring + ia + 1;
            tris.push_back(Eigen::Vector3i(i0, i2, i1));
            tris.push_back(Eigen::Vector3i(i1, i2, i3));
        }
    }

    // Build matrices.
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, static_cast<Eigen::Index>(verts.size()));
    for (std::size_t i = 0; i < verts.size(); ++i) V.col(static_cast<Eigen::Index>(i)) = verts[i];

    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, static_cast<Eigen::Index>(tris.size()));
    for (std::size_t i = 0; i < tris.size(); ++i) F.col(static_cast<Eigen::Index>(i)) = tris[i];

    return geometry::TriangleMesh(V, F);
}

geometry::TriangleMesh make_cylinder(double radius, double height,
                                      int radial_segments, int height_segments) {
    std::vector<Eigen::Vector3d> verts;
    std::vector<Eigen::Vector3i> tris;

    // Barrel vertices.
    for (int hi = 0; hi <= height_segments; ++hi) {
        double z = height * hi / height_segments;
        for (int ri = 0; ri < radial_segments; ++ri) {
            double angle = 2.0 * kPi * ri / radial_segments;
            verts.push_back(Eigen::Vector3d(
                radius * std::cos(angle), radius * std::sin(angle), z));
        }
    }

    // Barrel triangles.
    for (int hi = 0; hi < height_segments; ++hi) {
        for (int ri = 0; ri < radial_segments; ++ri) {
            int next_ri = (ri + 1) % radial_segments;
            int i0 = hi * radial_segments + ri;
            int i1 = hi * radial_segments + next_ri;
            int i2 = (hi + 1) * radial_segments + ri;
            int i3 = (hi + 1) * radial_segments + next_ri;
            tris.push_back(Eigen::Vector3i(i0, i1, i3));
            tris.push_back(Eigen::Vector3i(i0, i3, i2));
        }
    }

    // Bottom cap (z = 0).
    int bot_center = static_cast<int>(verts.size());
    verts.push_back(Eigen::Vector3d(0, 0, 0));
    for (int ri = 0; ri < radial_segments; ++ri) {
        int next = (ri + 1) % radial_segments;
        tris.push_back(Eigen::Vector3i(bot_center, next, ri));
    }

    // Top cap (z = height).
    int top_center = static_cast<int>(verts.size());
    verts.push_back(Eigen::Vector3d(0, 0, height));
    int top_ring = height_segments * radial_segments;
    for (int ri = 0; ri < radial_segments; ++ri) {
        int next = (ri + 1) % radial_segments;
        tris.push_back(Eigen::Vector3i(top_center, top_ring + ri, top_ring + next));
    }

    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, static_cast<Eigen::Index>(verts.size()));
    for (std::size_t i = 0; i < verts.size(); ++i) V.col(static_cast<Eigen::Index>(i)) = verts[i];
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, static_cast<Eigen::Index>(tris.size()));
    for (std::size_t i = 0; i < tris.size(); ++i) F.col(static_cast<Eigen::Index>(i)) = tris[i];

    return geometry::TriangleMesh(V, F);
}

geometry::TriangleMesh make_sphere(double radius, int u_segments, int v_segments) {
    std::vector<Eigen::Vector3d> verts;
    std::vector<Eigen::Vector3i> tris;

    // Vertices (excluding poles).
    for (int vi = 1; vi < v_segments; ++vi) {
        double phi = kPi * vi / v_segments;
        for (int ui = 0; ui < u_segments; ++ui) {
            double theta = 2.0 * kPi * ui / u_segments;
            verts.push_back(Eigen::Vector3d(
                radius * std::sin(phi) * std::cos(theta),
                radius * std::sin(phi) * std::sin(theta),
                radius * std::cos(phi)));
        }
    }

    // Poles.
    int south_pole = static_cast<int>(verts.size());
    verts.push_back(Eigen::Vector3d(0, 0, radius));     // north = first latitude row
    int north_pole = static_cast<int>(verts.size());
    verts.push_back(Eigen::Vector3d(0, 0, -radius));

    // Body quads.
    for (int vi = 0; vi < v_segments - 2; ++vi) {
        for (int ui = 0; ui < u_segments; ++ui) {
            int next_ui = (ui + 1) % u_segments;
            int i0 = vi * u_segments + ui;
            int i1 = vi * u_segments + next_ui;
            int i2 = (vi + 1) * u_segments + ui;
            int i3 = (vi + 1) * u_segments + next_ui;
            tris.push_back(Eigen::Vector3i(i0, i1, i3));
            tris.push_back(Eigen::Vector3i(i0, i3, i2));
        }
    }

    // North cap.
    for (int ui = 0; ui < u_segments; ++ui) {
        int next = (ui + 1) % u_segments;
        tris.push_back(Eigen::Vector3i(south_pole, ui, next));
    }

    // South cap.
    int last_ring = (v_segments - 2) * u_segments;
    for (int ui = 0; ui < u_segments; ++ui) {
        int next = (ui + 1) % u_segments;
        tris.push_back(Eigen::Vector3i(north_pole, last_ring + next, last_ring + ui));
    }

    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, static_cast<Eigen::Index>(verts.size()));
    for (std::size_t i = 0; i < verts.size(); ++i) V.col(static_cast<Eigen::Index>(i)) = verts[i];
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, static_cast<Eigen::Index>(tris.size()));
    for (std::size_t i = 0; i < tris.size(); ++i) F.col(static_cast<Eigen::Index>(i)) = tris[i];

    return geometry::TriangleMesh(V, F);
}

geometry::TriangleMesh make_curved_patch(double width, double depth,
                                          double amplitude, int u_segments, int v_segments) {
    std::vector<Eigen::Vector3d> verts;
    std::vector<Eigen::Vector3i> tris;

    for (int vi = 0; vi <= v_segments; ++vi) {
        double v = static_cast<double>(vi) / v_segments;
        for (int ui = 0; ui <= u_segments; ++ui) {
            double u = static_cast<double>(ui) / u_segments;
            double x = u * width;
            double y = v * depth;
            double z = amplitude * std::sin(kPi * u) * std::sin(kPi * v);
            verts.push_back(Eigen::Vector3d(x, y, z));
        }
    }

    for (int vi = 0; vi < v_segments; ++vi) {
        for (int ui = 0; ui < u_segments; ++ui) {
            int i00 = vi * (u_segments + 1) + ui;
            int i10 = i00 + 1;
            int i01 = i00 + (u_segments + 1);
            int i11 = i01 + 1;
            tris.push_back(Eigen::Vector3i(i00, i10, i11));
            tris.push_back(Eigen::Vector3i(i00, i11, i01));
        }
    }

    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, static_cast<Eigen::Index>(verts.size()));
    for (std::size_t i = 0; i < verts.size(); ++i) V.col(static_cast<Eigen::Index>(i)) = verts[i];
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, static_cast<Eigen::Index>(tris.size()));
    for (std::size_t i = 0; i < tris.size(); ++i) F.col(static_cast<Eigen::Index>(i)) = tris[i];

    return geometry::TriangleMesh(V, F);
}

geometry::TriangleMesh make_box(double sx, double sy, double sz) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, 8);
    V.col(0) = Eigen::Vector3d(0,  0,  0);
    V.col(1) = Eigen::Vector3d(sx, 0,  0);
    V.col(2) = Eigen::Vector3d(sx, sy, 0);
    V.col(3) = Eigen::Vector3d(0,  sy, 0);
    V.col(4) = Eigen::Vector3d(0,  0,  sz);
    V.col(5) = Eigen::Vector3d(sx, 0,  sz);
    V.col(6) = Eigen::Vector3d(sx, sy, sz);
    V.col(7) = Eigen::Vector3d(0,  sy, sz);

    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, 12);
    // -Z face (outward normal -Z)
    F.col(0)  = Eigen::Vector3i(0, 2, 1);
    F.col(1)  = Eigen::Vector3i(0, 3, 2);
    // +Z face
    F.col(2)  = Eigen::Vector3i(4, 5, 6);
    F.col(3)  = Eigen::Vector3i(4, 6, 7);
    // -Y face
    F.col(4)  = Eigen::Vector3i(0, 1, 5);
    F.col(5)  = Eigen::Vector3i(0, 5, 4);
    // +Y face
    F.col(6)  = Eigen::Vector3i(2, 3, 7);
    F.col(7)  = Eigen::Vector3i(2, 7, 6);
    // -X face
    F.col(8)  = Eigen::Vector3i(0, 4, 7);
    F.col(9)  = Eigen::Vector3i(0, 7, 3);
    // +X face
    F.col(10) = Eigen::Vector3i(1, 2, 6);
    F.col(11) = Eigen::Vector3i(1, 6, 5);

    return geometry::TriangleMesh(V, F);
}

// ── RPS configurations ───────────────────────────────────────────────────

RPSConfig full_rank6_rps_config(const geometry::TriangleMesh& geometry) {
    // Place 6 RPS points on the bounding box faces, each with a direction
    // lock along the outward face normal. This naturally spans 6 DOF:
    //   3 points on -Z face lock Z translation + X/Y rotations (3 DOF)
    //   2 points on -Y face lock Y translation + Z rotation    (2 DOF)
    //   1 point on -X face locks X translation                 (1 DOF)
    // Total: 6 DOF, rank 6.

    auto bbox_min = geometry.vertices().rowwise().minCoeff();
    auto bbox_max = geometry.vertices().rowwise().maxCoeff();
    auto center = (bbox_min + bbox_max) / 2.0;
    auto size = bbox_max - bbox_min;

    RPSConfig config;

    // 3 points on -Z face (primary datum A), normal = -Z.
    auto add_point = [&](const Eigen::Vector3d& pos, const Eigen::Vector3d& dir, double w) {
        registration::RPSConstraintPoint cp;
        cp.nominal = pos;
        cp.measured = pos;  // will be overwritten by projection in actual use
        registration::RPSDirectionLock lock;
        lock.direction = dir;
        lock.weight = w;
        cp.locks.push_back(lock);
        config.constraints.push_back(cp);
    };

    double zlo = bbox_min.z();
    double ylo = bbox_min.y();
    double xlo = bbox_min.x();

    // A1, A2, A3: three points on -Z face, spread for rotational constraint.
    add_point(Eigen::Vector3d(center.x() - size.x() * 0.3, center.y() - size.y() * 0.3, zlo),
              Eigen::Vector3d(0, 0, -1), 1.0);
    add_point(Eigen::Vector3d(center.x() + size.x() * 0.3, center.y() - size.y() * 0.3, zlo),
              Eigen::Vector3d(0, 0, -1), 1.0);
    add_point(Eigen::Vector3d(center.x(), center.y() + size.y() * 0.3, zlo),
              Eigen::Vector3d(0, 0, -1), 1.0);

    // B1, B2: two points on -Y face, spread for Z-rotation constraint.
    add_point(Eigen::Vector3d(center.x() - size.x() * 0.3, ylo, center.z()),
              Eigen::Vector3d(0, -1, 0), 1.0);
    add_point(Eigen::Vector3d(center.x() + size.x() * 0.3, ylo, center.z()),
              Eigen::Vector3d(0, -1, 0), 1.0);

    // C1: one point on -X face, locks X translation.
    add_point(Eigen::Vector3d(xlo, center.y(), center.z()),
              Eigen::Vector3d(-1, 0, 0), 1.0);

    return config;
}

RPSConfig under_constrained_rps_config(const geometry::TriangleMesh& geometry) {
    // Rank-5: all locks along Z only → X and Y translations unconstrained,
    // but actually 5 points with Z locks gives rank 3 (only Z + rot_x + rot_y).
    // For rank 5: use the rank-6 config but drop the last point (X lock).
    auto config = full_rank6_rps_config(geometry);
    if (!config.constraints.empty()) {
        config.constraints.pop_back();  // remove X-lock → rank 5
    }
    return config;
}

// ── Ground-truth transform factory ───────────────────────────────────────

geometry::RigidTransform make_known_transform(
        double rot_x_deg, double rot_y_deg, double rot_z_deg,
        double tx, double ty, double tz) {
    double rx = rot_x_deg * kPi / 180.0;
    double ry = rot_y_deg * kPi / 180.0;
    double rz = rot_z_deg * kPi / 180.0;

    // ZYX Euler angles → rotation matrix.
    Eigen::Matrix3d Rx, Ry, Rz;
    Rx << 1, 0, 0,  0, std::cos(rx), -std::sin(rx),  0, std::sin(rx), std::cos(rx);
    Ry << std::cos(ry), 0, std::sin(ry),  0, 1, 0,  -std::sin(ry), 0, std::cos(ry);
    Rz << std::cos(rz), -std::sin(rz), 0,  std::sin(rz), std::cos(rz), 0,  0, 0, 1;

    Eigen::Matrix3d R = Rz * Ry * Rx;
    Eigen::Vector3d t(tx, ty, tz);

    return geometry::RigidTransform::from_rotation_translation(R, t);
}

} // namespace alignmesh::test_harness
