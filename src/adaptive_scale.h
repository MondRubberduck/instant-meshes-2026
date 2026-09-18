/*
    adaptive_scale.h: Curvature-adaptive sizing field and target polycount management.
    
    This file is part of the modernization of Instant Meshes.
*/

#pragma once

#include "common.h"

struct AdaptiveScaleResult {
    Float global_scale;              // Baseline edge length
    VectorXf local_scale;            // Per-vertex target edge length
    VectorXf curvature;              // Per-vertex estimated mean curvature
    uint32_t target_face_count;      // Calibrated target face count
    uint32_t target_vertex_count;    // Calibrated target vertex count
    Float surface_area;              // Total surface area
};

class AdaptiveScaleManager {
public:
    /**
     * Compute a curvature-adaptive sizing field that accurately reaches the target poly count.
     * 
     * @param F Triangles (3 x M)
     * @param V Vertices (3 x N)
     * @param N Normals (3 x N)
     * @param A Dual vertex areas (N)
     * @param target_vertex_count Desired vertex count (-1 if using target_faces or scale)
     * @param target_face_count Desired face count (-1 if using target_vertices or scale)
     * @param target_scale Fixed uniform scale (-1 if computing from target counts)
     * @param adaptivity Curvature adaptivity factor in [0, 1]. 0 = uniform, 1 = strongly adaptive.
     * @param posy Position symmetry (4 for quads, 3/6 for triangles)
     */
    static AdaptiveScaleResult compute_scale_field(
        const MatrixXu &F,
        const MatrixXf &V,
        const MatrixXf &N,
        const VectorXf &A,
        int target_vertex_count,
        int target_face_count,
        Float target_scale,
        Float adaptivity,
        int posy = 4,
        bool pure_quad = true
    );

    /**
     * Compute discrete mean curvature using cotangent Laplace-Beltrami operator.
     */
    static VectorXf compute_mean_curvature(
        const MatrixXu &F,
        const MatrixXf &V,
        const VectorXf &A
    );

    /**
     * Smooth scalar field over mesh vertices using 1-ring neighborhood area weighting.
     */
    static VectorXf smooth_field(
        const MatrixXu &F,
        const VectorXf &A,
        const VectorXf &field,
        int iterations = 2
    );
};
