/*
    retopo_engine.h: Unified Modern Headless Retopology Pipeline API.
    
    This file is part of the modernization of Instant Meshes.
*/

#pragma once

#include "common.h"
#include "adaptive_scale.h"
#include "contour_guide.h"
#include "singularity_filter.h"
#include "quality_report.h"
#include <string>
#include <vector>

struct RetopoSettings {
    int rosy = 4;                           // 4 for quads, 2 for bidirectional, 6 for triangles
    int posy = 4;                           // 4 for quads, 3/6 for triangles
    int target_vertex_count = -1;           // Desired output vertex count
    int target_face_count = -1;             // Desired output face count
    Float scale = -1.0f;                    // Target edge length (if target counts not specified)
    Float adaptivity = 0.0f;                // Curvature adaptivity [0.0 = uniform, 1.0 = highly adaptive]
    Float crease_angle = -1.0f;             // Dihedral angle for sharp features (-1 = disabled)
    bool align_to_boundaries = true;        // Align quads to mesh open boundaries
    bool pure_quad = true;                  // Force pure quad output via 1 step subdivision
    bool deterministic = false;             // Deterministic execution mode
    int smooth_iterations = 2;              // Number of smoothing & ray reprojection steps
    bool filter_singularities = true;       // Modern QuadriFlow-inspired dipole cancellation
    bool mirror_x = false;                  // Mirror contours across X=0 plane for bilateral symmetry
    bool intrinsic = false;                 // Intrinsic mode with cotangent Laplacian weighting
    int knn_points = 10;                    // For point cloud mode
};

struct RetopoOutput {
    MatrixXf V;                             // Output vertex positions (3 x N)
    MatrixXu F;                             // Output polygon face indices (4 x M for quads, 3 x M for tris)
    MatrixXf N;                             // Output vertex normals (3 x N)
    MatrixXf Nf;                            // Output face normals (3 x M)
    uint32_t vertex_count = 0;
    uint32_t face_count = 0;
    Float edge_length = 0.0f;
    double elapsed_time_ms = 0.0;
    bool success = false;
    std::string error_message = "";
    MeshQualityReport quality_report;       // Production Quality Gates & Mesh Metrics
};

class RetopoEngine {
public:
    RetopoEngine();
    ~RetopoEngine();

    // Set input geometry from memory
    void set_mesh(const MatrixXu &F, const MatrixXf &V);

    // Load input geometry from file (OBJ, PLY, ALN)
    bool load_file(const std::string &filename);

    // Contour / topology flow controls
    void add_contour(const GuideContour &contour);
    void add_contour_points(const std::vector<Vector3f> &points, bool is_edge_loop = true, bool is_closed = false, Float weight = 1.0f);
    void clear_contours();
    size_t contour_count() const { return m_contour_system.count(); }

    // Execute retopology pipeline
    RetopoOutput execute(const RetopoSettings &settings, const ProgressCallback &progress = nullptr);

    // Export result to file
    static bool save_mesh(const std::string &filename, const RetopoOutput &output);

private:
    MatrixXu m_F;
    MatrixXf m_V;
    MatrixXf m_N;
    ContourGuideSystem m_contour_system;
};
