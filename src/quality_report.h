/*
    quality_report.h: Production Quality Gates & Mesh Quality Metrics Engine.
    
    This file is part of the modernization of Instant Meshes.
*/

#pragma once

#include "common.h"
#include <map>
#include <string>
#include <vector>

struct MeshQualityReport {
    // Face counts and quad ratio
    uint32_t total_faces = 0;
    uint32_t quad_faces = 0;
    uint32_t tri_faces = 0;
    uint32_t ngon_faces = 0;
    Float quad_ratio = 0.0f;               // Percentage of quads (0.0 to 100.0%)

    // Vertex & Valence statistics
    uint32_t total_vertices = 0;
    uint32_t interior_vertices = 0;
    uint32_t regular_valence_count = 0;    // Valence 4 for quads (or 6 for tris)
    Float regular_valence_ratio = 0.0f;    // Percentage of regular interior vertices
    std::map<int, uint32_t> valence_histogram; // Counts for valence 2, 3, 4, 5, 6, 7+

    // Edge length statistics
    uint32_t total_edges = 0;
    Float min_edge_length = 0.0f;
    Float max_edge_length = 0.0f;
    Float avg_edge_length = 0.0f;
    Float edge_aspect_ratio = 0.0f;        // max / min

    // Geometric distortion & Jacobian
    Float min_scaled_jacobian = 0.0f;      // Scaled Jacobian [-1.0 to 1.0]. > 0 means no fold-overs.
    Float avg_scaled_jacobian = 0.0f;
    uint32_t inverted_faces = 0;           // Faces with Jacobian <= 0

    // Topological integrity
    uint32_t boundary_edges = 0;           // Edges with 1 incident face
    uint32_t nonmanifold_edges = 0;        // Edges with > 2 incident faces
    uint32_t nonmanifold_vertices = 0;     // Vertices with non-disk 1-ring star
    bool is_watertight = false;            // boundary_edges == 0 && is_manifold
    bool is_manifold = false;              // nonmanifold_edges == 0 && nonmanifold_vertices == 0

    // Quality Gates (Production acceptance)
    bool passed_gates = false;

    // Format human-readable report
    std::string to_string() const;
};

class MeshQualityReporter {
public:
    // Analyze an extracted mesh and generate a comprehensive quality report
    static MeshQualityReport analyze(const MatrixXu &F, const MatrixXf &V, const MatrixXf &N = MatrixXf(), const MatrixXf &Nf = MatrixXf());

    // Print summary directly to stdout
    static void print_report(const MeshQualityReport &report);
};
