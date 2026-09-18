/*
    adaptive_scale.cpp: Curvature-adaptive sizing field and target polycount management.
    
    This file is part of the modernization of Instant Meshes.
*/

#include "adaptive_scale.h"
#include <algorithm>
#include <cmath>
#include <vector>

VectorXf AdaptiveScaleManager::compute_mean_curvature(
    const MatrixXu &F,
    const MatrixXf &V,
    const VectorXf &A
) {
    uint32_t n_vertices = V.cols();
    uint32_t n_faces = F.cols();

    MatrixXf laplacian = MatrixXf::Zero(3, n_vertices);

    for (uint32_t f = 0; f < n_faces; ++f) {
        uint32_t i0 = F(0, f);
        uint32_t i1 = F(1, f);
        uint32_t i2 = F(2, f);

        Vector3f v0 = V.col(i0);
        Vector3f v1 = V.col(i1);
        Vector3f v2 = V.col(i2);

        Vector3f e0 = v2 - v1;
        Vector3f e1 = v0 - v2;
        Vector3f e2 = v1 - v0;

        Vector3f normal = (v1 - v0).cross(v2 - v0);
        Float double_area = normal.norm();
        if (double_area < 1e-12f)
            continue;

        // Cotangents of opposite angles
        Float cot0 = -e1.dot(e2) / double_area;
        Float cot1 = -e2.dot(e0) / double_area;
        Float cot2 = -e0.dot(e1) / double_area;

        // Accumulate Laplace-Beltrami operator
        laplacian.col(i1) += cot2 * (v0 - v1);
        laplacian.col(i0) += cot2 * (v1 - v0);

        laplacian.col(i2) += cot0 * (v1 - v2);
        laplacian.col(i1) += cot0 * (v2 - v1);

        laplacian.col(i0) += cot1 * (v2 - v0);
        laplacian.col(i2) += cot1 * (v0 - v2);
    }

    VectorXf H(n_vertices);
    for (uint32_t i = 0; i < n_vertices; ++i) {
        Float dual_area = (A.size() > (int)i && A[i] > 1e-12f) ? A[i] : 1.0f;
        Vector3f hn = laplacian.col(i) / (2.0f * dual_area);
        H[i] = 0.5f * hn.norm();
    }

    return H;
}

VectorXf AdaptiveScaleManager::smooth_field(
    const MatrixXu &F,
    const VectorXf &A,
    const VectorXf &field,
    int iterations
) {
    uint32_t n_vertices = field.size();
    VectorXf current = field;
    VectorXf next(n_vertices);

    for (int iter = 0; iter < iterations; ++iter) {
        VectorXf weights = VectorXf::Zero(n_vertices);
        next.setZero();

        // Self contribution
        for (uint32_t i = 0; i < n_vertices; ++i) {
            Float w = (A.size() > (int)i) ? A[i] : 1.0f;
            next[i] += current[i] * w;
            weights[i] += w;
        }

        // 1-ring neighborhood accumulation from faces
        for (uint32_t f = 0; f < F.cols(); ++f) {
            for (int k = 0; k < 3; ++k) {
                uint32_t i = F(k, f);
                uint32_t j = F((k + 1) % 3, f);
                Float w = (A.size() > (int)j) ? A[j] : 1.0f;

                next[i] += current[j] * w;
                weights[i] += w;

                Float wi = (A.size() > (int)i) ? A[i] : 1.0f;
                next[j] += current[i] * wi;
                weights[j] += wi;
            }
        }

        for (uint32_t i = 0; i < n_vertices; ++i) {
            if (weights[i] > 1e-12f)
                next[i] /= weights[i];
            else
                next[i] = current[i];
        }

        current = next;
    }

    return current;
}

AdaptiveScaleResult AdaptiveScaleManager::compute_scale_field(
    const MatrixXu &F,
    const MatrixXf &V,
    const MatrixXf &/*N*/,
    const VectorXf &A,
    int target_vertex_count,
    int target_face_count,
    Float target_scale,
    Float adaptivity,
    int posy,
    bool pure_quad
) {
    AdaptiveScaleResult result;
    uint32_t n_vertices = V.cols();

    // 1. Calculate total surface area
    Float total_area = 0.0f;
    if (A.size() == (int)n_vertices) {
        total_area = A.sum();
    } else {
        for (uint32_t f = 0; f < F.cols(); ++f) {
            Vector3f v0 = V.col(F(0, f)), v1 = V.col(F(1, f)), v2 = V.col(F(2, f));
            total_area += 0.5f * (v1 - v0).cross(v2 - v0).norm();
        }
    }
    result.surface_area = total_area;

    // 2. Resolve target counts
    if (target_scale <= 0 && target_vertex_count <= 0 && target_face_count <= 0) {
        target_vertex_count = std::max(32, (int)n_vertices / 16);
    }

    if (target_scale > 0) {
        // Direct physical scale given (coarse edge length)
        Float coarse_face_area = (posy == 4) ? (target_scale * target_scale) : (std::sqrt(3.0f) / 4.0f * target_scale * target_scale);
        uint32_t coarse_faces = (uint32_t)std::round(total_area / coarse_face_area);
        if (posy == 4 && pure_quad) {
            target_face_count = coarse_faces * 4;
            target_vertex_count = target_face_count;
        } else if (posy == 4) {
            target_face_count = coarse_faces;
            target_vertex_count = coarse_faces;
        } else {
            target_face_count = coarse_faces;
            target_vertex_count = coarse_faces / 2;
        }
    } else if (target_face_count > 0) {
        target_vertex_count = (posy == 4) ? target_face_count : (target_face_count / 2);
    } else if (target_vertex_count > 0) {
        target_face_count = (posy == 4) ? target_vertex_count : (target_vertex_count * 2);
    }

    result.target_face_count = std::max(4u, (uint32_t)target_face_count);
    result.target_vertex_count = std::max(4u, (uint32_t)target_vertex_count);

    adaptivity = std::max(0.0f, std::min(1.0f, adaptivity));

    // 3. Compute curvature and adaptive density modulation
    VectorXf raw_H = compute_mean_curvature(F, V, A);
    VectorXf smoothed_H = smooth_field(F, A, raw_H, 2);
    result.curvature = smoothed_H;

    VectorXf rho = VectorXf::Ones(n_vertices);

    if (adaptivity > 0.001f && n_vertices > 0) {
        // Robust percentiles
        std::vector<Float> sorted_H(n_vertices);
        for (uint32_t i = 0; i < n_vertices; ++i)
            sorted_H[i] = smoothed_H[i];
        std::sort(sorted_H.begin(), sorted_H.end());

        Float p5 = sorted_H[(size_t)(0.05f * n_vertices)];
        Float p95 = sorted_H[(size_t)(0.95f * n_vertices)];
        Float range = std::max(1e-5f, p95 - p5);

        // Compute relative weights centered around median/mean
        Float sum_weighted = 0.0f;
        for (uint32_t i = 0; i < n_vertices; ++i) {
            Float norm_h = std::max(0.0f, std::min(1.0f, (smoothed_H[i] - p5) / range));
            // Power modulation: exponential density curve based on curvature
            rho[i] = std::exp(adaptivity * (norm_h - 0.5f) * 2.0f);
            Float dual_area = (A.size() > (int)i) ? A[i] : (total_area / n_vertices);
            sum_weighted += dual_area * rho[i];
        }

        // Strictly normalize rho so that integral(rho * dA) == total_area
        if (sum_weighted > 1e-6f) {
            Float norm_factor = total_area / sum_weighted;
            for (uint32_t i = 0; i < n_vertices; ++i)
                rho[i] *= norm_factor;
        }
    }

    // 4. Calibrate baseline scale so integrated quad density equals target_face_count
    Float base_scale = 0.0f;
    if (target_scale > 0) {
        base_scale = target_scale;
    } else {
        // In pure quad mode (posy == 4 && pure_quad), Step 9 regular subdivision subdivides each coarse quad into 4 quads!
        // Therefore, the coarse hierarchy solver must target F_coarse = F_target / 4.
        uint32_t coarse_face_target = result.target_face_count;
        if (posy == 4 && pure_quad) {
            coarse_face_target = std::max(1u, (uint32_t)std::round(result.target_face_count / 4.0f));
        }

        Float base_face_area = total_area / (Float)coarse_face_target;
        base_scale = (posy == 4) 
            ? std::sqrt(base_face_area) 
            : (2.0f * std::sqrt(base_face_area * std::sqrt(1.0f / 3.0f)));
    }

    result.global_scale = base_scale;
    result.local_scale.resize(n_vertices);

    for (uint32_t i = 0; i < n_vertices; ++i) {
        result.local_scale[i] = base_scale / std::sqrt(rho[i]);
    }

    return result;
}
