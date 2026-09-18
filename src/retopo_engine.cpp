/*
    retopo_engine.cpp: Unified Modern Headless Retopology Pipeline API.
    
    This file is part of the modernization of Instant Meshes.
*/

#include "retopo_engine.h"
#include "meshio.h"
#include "dedge.h"
#include "subdivide.h"
#include "meshstats.h"
#include "adjacency.h"
#include "normal.h"
#include "extract.h"
#include "field.h"
#include "bvh.h"
#include <iostream>

RetopoEngine::RetopoEngine() {
}

RetopoEngine::~RetopoEngine() {
}

void RetopoEngine::set_mesh(const MatrixXu &F, const MatrixXf &V) {
    m_F = F;
    m_V = V;
    m_N.resize(3, V.cols());
    m_N.setZero();
}

bool RetopoEngine::load_file(const std::string &filename) {
    try {
        load_mesh_or_pointcloud(filename, m_F, m_V, m_N);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "[RetopoEngine] Failed to load mesh: " << e.what() << std::endl;
        return false;
    }
}

void RetopoEngine::add_contour(const GuideContour &contour) {
    m_contour_system.add_contour(contour);
}

void RetopoEngine::add_contour_points(const std::vector<Vector3f> &points, bool is_edge_loop, bool is_closed, Float weight) {
    GuideContour c;
    c.points = points;
    c.is_edge_loop = is_edge_loop;
    c.is_closed = is_closed;
    c.weight = weight;
    m_contour_system.add_contour(c);
}

void RetopoEngine::clear_contours() {
    m_contour_system.clear();
}

RetopoOutput RetopoEngine::execute(const RetopoSettings &settings, const ProgressCallback &progress) {
    RetopoOutput out;
    Timer<> timer;

    if (m_V.cols() == 0) {
        out.success = false;
        out.error_message = "Input mesh is empty.";
        return out;
    }

    try {
        MatrixXu F = m_F;
        MatrixXf V = m_V;
        MatrixXf N = m_N;
        VectorXf A;
        std::set<uint32_t> crease_in, crease_out;
        BVH *bvh = nullptr;
        AdjacencyMatrix adj = nullptr;

        bool pointcloud = (F.size() == 0);
        MeshStats stats = compute_mesh_stats(F, V, settings.deterministic);

        if (pointcloud) {
            bvh = new BVH(&F, &V, &N, stats.mAABB);
            bvh->build();
            adj = generate_adjacency_matrix_pointcloud(V, N, bvh, stats, settings.knn_points, settings.deterministic);
            A.resize(V.cols());
            A.setConstant(1.0f);
        }

        // 1. Curvature-Adaptive Sizing and Target Poly Count
        AdaptiveScaleResult scale_res = AdaptiveScaleManager::compute_scale_field(
            F, V, N, A,
            settings.target_vertex_count,
            settings.target_face_count,
            settings.scale,
            settings.adaptivity,
            settings.posy,
            settings.pure_quad
        );

        Float scale = scale_res.global_scale;
        out.edge_length = scale;

        std::cout << "[RetopoEngine] Goals: "
                  << scale_res.target_vertex_count << " vertices, "
                  << scale_res.target_face_count << " faces, "
                  << "scale = " << scale << ", adaptivity = " << settings.adaptivity
                  << ", contours = " << m_contour_system.count() << std::endl;

        MultiResolutionHierarchy mRes;

        if (!pointcloud) {
            VectorXu V2E, E2E;
            VectorXb boundary, nonManifold;

            // Subdivide input mesh if resolution is too coarse for desired target density
            if (stats.mMaximumEdgeLength * 2.0f > scale || stats.mMaximumEdgeLength > stats.mAverageEdgeLength * 2.0f) {
                build_dedge(F, V, V2E, E2E, boundary, nonManifold);
                subdivide(F, V, V2E, E2E, boundary, nonManifold,
                          std::min(scale / 2.0f, (Float)stats.mAverageEdgeLength * 2.0f),
                          settings.deterministic);
            }

            build_dedge(F, V, V2E, E2E, boundary, nonManifold);
            if (settings.intrinsic) {
                std::cout << "[RetopoEngine] Computing intrinsic cotangent Laplacian adjacency matrix..." << std::endl;
                adj = generate_adjacency_matrix_cotan(F, V, V2E, E2E, nonManifold);
            } else {
                adj = generate_adjacency_matrix_uniform(F, V2E, E2E, nonManifold);
            }

            if (settings.crease_angle >= 0.0f)
                generate_crease_normals(F, V, V2E, E2E, boundary, nonManifold, settings.crease_angle, N, crease_in);
            else
                generate_smooth_normals(F, V, V2E, E2E, nonManifold, N);

            compute_dual_vertex_areas(F, V, V2E, E2E, nonManifold, A);
            mRes.setE2E(std::move(E2E));
        }

        // 2. Build multi-resolution hierarchy
        mRes.setAdj(std::move(adj));
        mRes.setF(std::move(F));
        mRes.setV(std::move(V));
        mRes.setA(std::move(A));
        mRes.setN(std::move(N));
        mRes.setScale(scale);
        mRes.build(settings.deterministic, progress);
        mRes.resetSolution();
        mRes.clearConstraints();

        // 3. Build spatial acceleration BVH
        if (bvh) {
            bvh->setData(&mRes.F(), &mRes.V(), &mRes.N());
        } else {
            bvh = new BVH(&mRes.F(), &mRes.V(), &mRes.N(), stats.mAABB);
            bvh->build();
        }

        // 4. Boundary alignment
        if (settings.align_to_boundaries && !pointcloud) {
            for (uint32_t i = 0; i < 3 * mRes.F().cols(); ++i) {
                if (mRes.E2E()[i] == INVALID) {
                    uint32_t i0 = mRes.F()(i % 3, i / 3);
                    uint32_t i1 = mRes.F()((i + 1) % 3, i / 3);
                    Vector3f p0 = mRes.V().col(i0), p1 = mRes.V().col(i1);
                    Vector3f edge = p1 - p0;
                    if (edge.squaredNorm() > 0.0f) {
                        edge.normalize();
                        mRes.CO().col(i0) = p0;
                        mRes.CO().col(i1) = p1;
                        mRes.CQ().col(i0) = mRes.CQ().col(i1) = edge;
                        mRes.CQw()[i0] = mRes.CQw()[i1] = mRes.COw()[i0] = mRes.COw()[i1] = 1.0f;
                    }
                }
            }
            mRes.propagateConstraints(settings.rosy, settings.posy);
        }

        // 5. Apply user contour guides (orientation flow + edge-loop snapping)
        if (m_contour_system.count() > 0) {
            m_contour_system.set_mirror_x(settings.mirror_x);
            m_contour_system.apply_to_hierarchy(mRes, bvh, settings.rosy, settings.posy);
        }

        // 6. Optimize orientation field
        Optimizer optimizer(mRes, false);
        optimizer.setRoSy(settings.rosy);
        optimizer.setPoSy(settings.posy);
        optimizer.setExtrinsic(!settings.intrinsic);

        optimizer.optimizeOrientations(-1);
        optimizer.notify();
        optimizer.wait();

        // 7. Modern Singularity Regularization & Dipole Cancellation (QuadriFlow-inspired)
        if (settings.filter_singularities && settings.rosy == 4 && settings.posy == 4) {
            SingularityFilter::regularize_singularities(mRes, settings.rosy, scale * 2.5f);
        }

        // 8. Optimize position field
        optimizer.optimizePositions(-1);
        optimizer.notify();
        optimizer.wait();
        optimizer.shutdown();

        // 9. Mesh extraction
        MatrixXf O_extr, N_extr, Nf_extr;
        std::vector<std::vector<TaggedLink>> adj_extr;
        extract_graph(mRes, true, settings.rosy, settings.posy, adj_extr, O_extr, N_extr,
                      crease_in, crease_out, settings.deterministic);

        MatrixXu F_extr;
        extract_faces(adj_extr, O_extr, N_extr, Nf_extr, F_extr, settings.posy,
                      mRes.scale(), crease_out, true, settings.pure_quad, bvh, settings.smooth_iterations);

        if (bvh)
            delete bvh;

        out.V = std::move(O_extr);
        out.F = std::move(F_extr);
        out.N = std::move(N_extr);
        out.Nf = std::move(Nf_extr);
        out.vertex_count = (uint32_t)out.V.cols();
        out.face_count = (uint32_t)out.F.cols();
        out.elapsed_time_ms = timer.value();
        out.success = true;

        // 10. Production Quality Gates & Mesh Metrics Analysis
        out.quality_report = MeshQualityReporter::analyze(out.F, out.V, out.N, out.Nf);

        std::cout << "[RetopoEngine] Success! Extracted " 
                  << out.vertex_count << " vertices and " 
                  << out.face_count << " faces in " 
                  << timeString(out.elapsed_time_ms) << "." << std::endl;

        MeshQualityReporter::print_report(out.quality_report);

        return out;
    } catch (const std::exception &e) {
        out.success = false;
        out.error_message = e.what();
        std::cerr << "[RetopoEngine] Error: " << e.what() << std::endl;
        return out;
    }
}

bool RetopoEngine::save_mesh(const std::string &filename, const RetopoOutput &output) {
    if (!output.success || output.V.cols() == 0)
        return false;
    try {
        write_mesh(filename, output.F, output.V, MatrixXf(), output.Nf);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "[RetopoEngine] Failed to write mesh: " << e.what() << std::endl;
        return false;
    }
}
