# --- Unit tests (GoogleTest) --------------------------------------------------

add_executable(test_compensated_sum test_compensated_sum.cpp)
target_link_libraries(test_compensated_sum PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_compensated_sum PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_compensated_sum COMMAND test_compensated_sum)

add_executable(test_seeded_rng test_seeded_rng.cpp)
target_link_libraries(test_seeded_rng PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_seeded_rng PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_seeded_rng COMMAND test_seeded_rng)

add_executable(test_environment_fingerprint test_environment_fingerprint.cpp)
target_link_libraries(test_environment_fingerprint PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_environment_fingerprint PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_environment_fingerprint COMMAND test_environment_fingerprint)

add_executable(test_rigid_transform test_rigid_transform.cpp)
target_link_libraries(test_rigid_transform PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_rigid_transform PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_rigid_transform COMMAND test_rigid_transform)

add_executable(test_point_cloud test_point_cloud.cpp)
target_link_libraries(test_point_cloud PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_point_cloud PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_point_cloud COMMAND test_point_cloud)

add_executable(test_triangle_mesh test_triangle_mesh.cpp)
target_link_libraries(test_triangle_mesh PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_triangle_mesh PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_triangle_mesh COMMAND test_triangle_mesh)

add_executable(test_io test_io.cpp)
target_link_libraries(test_io PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_io PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_io COMMAND test_io)

add_executable(test_mesh_validation test_mesh_validation.cpp)
target_link_libraries(test_mesh_validation PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_mesh_validation PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_mesh_validation COMMAND test_mesh_validation)

add_executable(test_kabsch test_kabsch.cpp)
target_link_libraries(test_kabsch PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_kabsch PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_kabsch COMMAND test_kabsch)

add_executable(test_spatial test_spatial.cpp)
target_link_libraries(test_spatial PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_spatial PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_spatial COMMAND test_spatial)

add_executable(test_fine_registration test_fine_registration.cpp)
target_link_libraries(test_fine_registration PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_fine_registration PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_fine_registration COMMAND test_fine_registration)

add_executable(test_robustness test_robustness.cpp)
target_link_libraries(test_robustness PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_robustness PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_robustness COMMAND test_robustness)

add_executable(test_global_registration test_global_registration.cpp)
target_link_libraries(test_global_registration PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_global_registration PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_global_registration COMMAND test_global_registration)

add_executable(test_observability test_observability.cpp)
target_link_libraries(test_observability PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_observability PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_observability COMMAND test_observability)

add_executable(test_hybrid_pipeline test_hybrid_pipeline.cpp)
target_link_libraries(test_hybrid_pipeline PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_hybrid_pipeline PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_hybrid_pipeline COMMAND test_hybrid_pipeline)

add_executable(test_deviation test_deviation.cpp)
target_link_libraries(test_deviation PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_deviation PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_deviation COMMAND test_deviation)

add_executable(test_precision_tier test_precision_tier.cpp)
target_link_libraries(test_precision_tier PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_precision_tier PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_precision_tier COMMAND test_precision_tier)

add_executable(test_feasibility test_feasibility.cpp)
target_link_libraries(test_feasibility PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_feasibility PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_feasibility COMMAND test_feasibility)

add_executable(test_gum_uncertainty test_gum_uncertainty.cpp)
target_link_libraries(test_gum_uncertainty PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_gum_uncertainty PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_gum_uncertainty COMMAND test_gum_uncertainty)

add_executable(test_decision_rule test_decision_rule.cpp)
target_link_libraries(test_decision_rule PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_decision_rule PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_decision_rule COMMAND test_decision_rule)

add_executable(test_cad_surface test_cad_surface.cpp)
target_link_libraries(test_cad_surface PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_cad_surface PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_cad_surface COMMAND test_cad_surface)

add_executable(test_cad_localization test_cad_localization.cpp)
target_link_libraries(test_cad_localization PRIVATE alignmesh_core GTest::gtest_main)
if(ALIGNMESH_WITH_STEP)
    # Test-only OCCT modeling libs for synthetic shape construction.
    target_link_libraries(test_cad_localization PRIVATE TKPrim TKBO TKBool TKFillet)
endif()
target_compile_options(test_cad_localization PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_cad_localization COMMAND test_cad_localization)

add_executable(test_feature_fitting test_feature_fitting.cpp)
target_link_libraries(test_feature_fitting PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_feature_fitting PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_feature_fitting COMMAND test_feature_fitting)

add_executable(test_datum_alignment test_datum_alignment.cpp)
target_link_libraries(test_datum_alignment PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_datum_alignment PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_datum_alignment COMMAND test_datum_alignment)

add_executable(test_service_firewall test_service_firewall.cpp)
target_link_libraries(test_service_firewall PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_service_firewall PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_service_firewall COMMAND test_service_firewall)

add_executable(test_reporting test_reporting.cpp)
target_link_libraries(test_reporting PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_reporting PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_reporting COMMAND test_reporting)

add_executable(test_heuristic_assist test_heuristic_assist.cpp)
target_link_libraries(test_heuristic_assist PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_heuristic_assist PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_heuristic_assist COMMAND test_heuristic_assist)

add_executable(test_rps_projection test_rps_projection.cpp)
target_link_libraries(test_rps_projection PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_rps_projection PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_rps_projection COMMAND test_rps_projection)

add_executable(test_rps_alignment test_rps_alignment.cpp)
target_link_libraries(test_rps_alignment PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_rps_alignment PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_rps_alignment COMMAND test_rps_alignment)

add_executable(test_pointcloud_io test_pointcloud_io.cpp)
target_link_libraries(test_pointcloud_io PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_pointcloud_io PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_pointcloud_io COMMAND test_pointcloud_io)

add_executable(test_validation_harness test_validation_harness.cpp)
target_link_libraries(test_validation_harness PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_validation_harness PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_validation_harness COMMAND test_validation_harness)

add_executable(test_reference_geometry test_reference_geometry.cpp)
target_link_libraries(test_reference_geometry PRIVATE alignmesh_core GTest::gtest_main)
target_compile_options(test_reference_geometry PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_reference_geometry COMMAND test_reference_geometry)

# Synthetic ground-truth test harness (shared library + self-tests).
add_library(test_harness_gt STATIC test_harness_gt.cpp test_harness_gt.h)
target_link_libraries(test_harness_gt PUBLIC alignmesh_core)
target_include_directories(test_harness_gt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_harness_gt_self test_harness_gt_self.cpp)
target_link_libraries(test_harness_gt_self PRIVATE test_harness_gt GTest::gtest_main)
target_compile_options(test_harness_gt_self PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_harness_gt_self COMMAND test_harness_gt_self)

add_executable(test_rps_solver test_rps_solver.cpp)
target_link_libraries(test_rps_solver PRIVATE test_harness_gt GTest::gtest_main)
target_compile_options(test_rps_solver PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_rps_solver COMMAND test_rps_solver)

add_executable(test_rps_e2e test_rps_e2e.cpp)
target_link_libraries(test_rps_e2e PRIVATE test_harness_gt GTest::gtest_main)
target_compile_options(test_rps_e2e PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_rps_e2e COMMAND test_rps_e2e)

add_executable(test_rps_coupling test_rps_coupling.cpp)
target_link_libraries(test_rps_coupling PRIVATE test_harness_gt GTest::gtest_main)
target_compile_options(test_rps_coupling PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_rps_coupling COMMAND test_rps_coupling)

add_executable(test_prealign test_prealign.cpp)
target_link_libraries(test_prealign PRIVATE test_harness_gt GTest::gtest_main)
target_compile_definitions(test_prealign PRIVATE
    ALIGNMESH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(test_prealign PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_prealign COMMAND test_prealign)

add_executable(test_acceptance test_acceptance.cpp)
target_link_libraries(test_acceptance PRIVATE test_harness_gt GTest::gtest_main)
target_compile_definitions(test_acceptance PRIVATE
    ALIGNMESH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(test_acceptance PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_acceptance COMMAND test_acceptance)

add_executable(test_rps_failsafe test_rps_failsafe.cpp)
target_link_libraries(test_rps_failsafe PRIVATE test_harness_gt GTest::gtest_main)
target_compile_options(test_rps_failsafe PRIVATE ${ALIGNMESH_WARNING_FLAGS})
add_test(NAME test_rps_failsafe COMMAND test_rps_failsafe)

# Diagnostic tools (not CTests — run manually for profiling)
add_executable(diag_step_deviation diag_step_deviation.cpp)
target_link_libraries(diag_step_deviation PRIVATE alignmesh_core)

add_executable(diag_alignment_sanity diag_alignment_sanity.cpp)
target_link_libraries(diag_alignment_sanity PRIVATE alignmesh_core)

add_executable(diag_fallback_count diag_fallback_count.cpp)
target_link_libraries(diag_fallback_count PRIVATE alignmesh_core)

add_executable(diag_ground_truth diag_ground_truth.cpp)
target_link_libraries(diag_ground_truth PRIVATE alignmesh_core)

add_executable(diag_rps_radius diag_rps_radius.cpp)
target_link_libraries(diag_rps_radius PRIVATE alignmesh_core)

add_executable(diag_rps_points diag_rps_points.cpp)
target_link_libraries(diag_rps_points PRIVATE alignmesh_core)

# --- Reproducibility manifest generator --------------------------------------
# Not a CTest test — invoked by ci/build_and_test.sh to produce deterministic
# numeric output that is hashed and compared across runs.
add_executable(repro_manifest repro_manifest.cpp)
target_link_libraries(repro_manifest PRIVATE alignmesh_core)
target_compile_options(repro_manifest PRIVATE ${ALIGNMESH_WARNING_FLAGS})
