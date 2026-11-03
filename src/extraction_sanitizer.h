/*
    extraction_sanitizer.h: BSD-native QEx-style Extraction Sanitization & Topological Hardening.
    
    This file is part of the modernization of Instant Meshes.
*/

#pragma once

#include "common.h"
#include <set>
#include <vector>

class BVH;

class ExtractionSanitizer {
public:
    // Sanitize coarse extracted faces before smoothing (Step 7/8/9 -> Step 10)
    // - Resolves collapsed quad edges
    // - Removes duplicate/degenerate faces
    // - Realigns inverted quad windings
    // - Splits non-manifold pinch vertices into 2-manifold disks
    static size_t sanitize_coarse_quads(
        MatrixXu &F,
        MatrixXf &O,
        MatrixXf &N,
        std::set<uint32_t> &crease
    );

    // Sanitize and verify final mesh after subdivision & smoothing (Step 10 -> export)
    // - Removes degenerate zero-area quads
    // - Untangles reflex corners only when all incident faces improve in Scaled Jacobian
    // - Re-projects adjusted vertices onto the input surface BVH and updates face normals Nf
    static size_t sanitize_final_mesh(
        MatrixXu &F,
        MatrixXf &O,
        MatrixXf &N,
        MatrixXf &Nf,
        BVH *bvh = nullptr,
        Float scale = 0.0f
    );
};
