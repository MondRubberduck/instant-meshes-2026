/*
    quality_report.cpp: Production Quality Gates & Mesh Quality Metrics Engine.
    
    This file is part of the modernization of Instant Meshes.
*/

#include "quality_report.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <set>
#include <algorithm>

MeshQualityReport MeshQualityReporter::analyze(const MatrixXu &F, const MatrixXf &V, const MatrixXf &N, const MatrixXf &Nf) {
    MeshQualityReport r;
    r.total_faces = (uint32_t)F.cols();
    r.total_vertices = (uint32_t)V.cols();

    if (r.total_faces == 0 || r.total_vertices == 0)
        return r;

    // 1. Face classification
    for (uint32_t f = 0; f < r.total_faces; ++f) {
        int rows = (int)F.rows();
        if (rows == 4) {
            if (F(2, f) == F(3, f)) {
                r.tri_faces++;
            } else {
                // Check if 4 vertices are distinct
                uint32_t v0 = F(0, f), v1 = F(1, f), v2 = F(2, f), v3 = F(3, f);
                if (v0 != v1 && v0 != v2 && v0 != v3 && v1 != v2 && v1 != v3 && v2 != v3) {
                    r.quad_faces++;
                } else {
                    r.ngon_faces++; // Degenerate face
                }
            }
        } else if (rows == 3) {
            r.tri_faces++;
        } else {
            r.ngon_faces++;
        }
    }

    r.quad_ratio = r.total_faces > 0 ? ((Float)r.quad_faces / (Float)r.total_faces) * 100.0f : 0.0f;

    /* Regular interior valence is 4 on quad meshes but 6 on triangle meshes;
       pick the expectation from the dominant face type. */
    const int expected_valence = (r.tri_faces > r.quad_faces) ? 6 : 4;

    // 2. Build Edge connectivity and vertex adjacency
    struct UndirectedEdge {
        uint32_t v0, v1;
        bool operator<(const UndirectedEdge &o) const {
            return v0 < o.v0 || (v0 == o.v0 && v1 < o.v1);
        }
    };

    std::map<UndirectedEdge, std::vector<uint32_t>> edge_to_faces;
    std::vector<std::set<uint32_t>> vertex_neighbors(r.total_vertices);
    std::vector<std::vector<uint32_t>> vertex_faces(r.total_vertices);

    for (uint32_t f = 0; f < r.total_faces; ++f) {
        int k = (F.rows() == 4 && F(2, f) == F(3, f)) ? 3 : (int)F.rows();
        for (int i = 0; i < k; ++i) {
            uint32_t u = F(i, f);
            uint32_t v = F((i + 1) % k, f);
            if (u >= r.total_vertices || v >= r.total_vertices || u == v)
                continue;

            UndirectedEdge e{std::min(u, v), std::max(u, v)};
            edge_to_faces[e].push_back(f);
            vertex_neighbors[u].insert(v);
            vertex_neighbors[v].insert(u);
            vertex_faces[u].push_back(f);
        }
    }

    r.total_edges = (uint32_t)edge_to_faces.size();

    // 3. Edge length statistics and Boundary/Manifoldness
    std::set<uint32_t> boundary_vertices;
    Float sum_edge_length = 0.0f;
    r.min_edge_length = 1e30f;
    r.max_edge_length = 0.0f;

    for (const auto &pair : edge_to_faces) {
        const UndirectedEdge &e = pair.first;
        const auto &faces = pair.second;

        Float len = (V.col(e.v0) - V.col(e.v1)).norm();
        sum_edge_length += len;
        if (len < r.min_edge_length) r.min_edge_length = len;
        if (len > r.max_edge_length) r.max_edge_length = len;

        if (faces.size() == 1) {
            r.boundary_edges++;
            boundary_vertices.insert(e.v0);
            boundary_vertices.insert(e.v1);
        } else if (faces.size() > 2) {
            r.nonmanifold_edges++;
        }
    }

    if (r.total_edges > 0) {
        r.avg_edge_length = sum_edge_length / (Float)r.total_edges;
        r.edge_aspect_ratio = r.min_edge_length > 1e-12f ? (r.max_edge_length / r.min_edge_length) : 0.0f;
    } else {
        r.min_edge_length = 0.0f;
    }

    // 4. Vertex Valence and Non-manifold vertex check
    for (uint32_t v = 0; v < r.total_vertices; ++v) {
        int valence = (int)vertex_neighbors[v].size();
        r.valence_histogram[valence]++;

        bool is_boundary = boundary_vertices.find(v) != boundary_vertices.end();
        /* Isolated vertices (referenced by no face) are neither interior nor boundary */
        if (!is_boundary && valence > 0) {
            r.interior_vertices++;
            if (valence == expected_valence)
                r.regular_valence_count++;
        }

        // Check if vertex star is 2-manifold (single connected fan)
        const auto &vf = vertex_faces[v];
        if (vf.size() > 1) {
            // Count boundary edges incident to v among faces in vf
            std::map<uint32_t, int> local_edge_count;
            for (uint32_t f : vf) {
                int k = (F.rows() == 4 && F(2, f) == F(3, f)) ? 3 : (int)F.rows();
                for (int i = 0; i < k; ++i) {
                    if (F(i, f) == v) {
                        uint32_t prev = F((i + k - 1) % k, f);
                        uint32_t next = F((i + 1) % k, f);
                        local_edge_count[prev]++;
                        local_edge_count[next]++;
                    }
                }
            }
            /* Count each offending vertex once, not once per offending edge */
            bool nm_vertex = false;
            int endpoints = 0;
            for (const auto &p : local_edge_count) {
                if (p.second == 1)
                    endpoints++;
                else if (p.second > 2)
                    nm_vertex = true;
            }
            if (endpoints > 2)
                nm_vertex = true;
            if (nm_vertex)
                r.nonmanifold_vertices++;
        }
    }

    r.regular_valence_ratio = r.interior_vertices > 0
        ? ((Float)r.regular_valence_count / (Float)r.interior_vertices) * 100.0f
        : 0.0f;

    // 5. Scaled Jacobian & Inverted Face Detection
    r.min_scaled_jacobian = 1.0f;
    Float sum_jacobian = 0.0f;
    uint32_t evaluated_faces = 0;

    for (uint32_t f = 0; f < r.total_faces; ++f) {
        if (F.rows() != 4 || F(2, f) == F(3, f))
            continue;

        uint32_t idx[4] = { F(0, f), F(1, f), F(2, f), F(3, f) };
        if (idx[0] >= r.total_vertices || idx[1] >= r.total_vertices ||
            idx[2] >= r.total_vertices || idx[3] >= r.total_vertices)
            continue;

        Vector3f p[4] = { V.col(idx[0]), V.col(idx[1]), V.col(idx[2]), V.col(idx[3]) };

        Float face_min_j = 1.0f;
        for (int i = 0; i < 4; ++i) {
            Vector3f e_in = p[i] - p[(i + 3) % 4];
            Vector3f e_out = p[(i + 1) % 4] - p[i];
            Float l_in = e_in.norm();
            Float l_out = e_out.norm();
            if (l_in < 1e-10f || l_out < 1e-10f) {
                face_min_j = -1.0f;
                break;
            }
            Vector3f corner_norm;
            if (N.cols() == r.total_vertices) {
                corner_norm = N.col(idx[i]);
            } else if (Nf.cols() == r.total_faces) {
                corner_norm = Nf.col(f);
            } else {
                corner_norm = (p[1] - p[0]).cross(p[3] - p[0]) + (p[3] - p[2]).cross(p[1] - p[2]);
                Float cn = corner_norm.norm();
                if (cn < 1e-12f) {
                    /* Bilinear normal cancels on a planar self-touching quad:
                       classify it as degenerate rather than perfectly regular. */
                    face_min_j = -1.0f;
                    break;
                }
                corner_norm /= cn;
            }
            Float j_val = e_in.cross(e_out).dot(corner_norm) / (l_in * l_out);
            if (j_val < face_min_j)
                face_min_j = j_val;
        }

        if (face_min_j <= 0.0f)
            r.inverted_faces++;

        if (face_min_j < r.min_scaled_jacobian)
            r.min_scaled_jacobian = face_min_j;

        sum_jacobian += face_min_j;
        evaluated_faces++;
    }

    if (evaluated_faces > 0)
        r.avg_scaled_jacobian = sum_jacobian / (Float)evaluated_faces;

    // 6. Quality Gates & Production Acceptance
    r.is_manifold = (r.nonmanifold_edges == 0 && r.nonmanifold_vertices == 0);
    r.is_watertight = (r.boundary_edges == 0 && r.is_manifold);
    r.passed_gates = (r.quad_ratio >= 90.0f && r.is_manifold && r.inverted_faces <= std::max(3u, (uint32_t)(r.total_faces * 0.01f)));

    return r;
}

std::string MeshQualityReport::to_string() const {
    std::ostringstream oss;
    oss << "\n+============================================================+\n";
    oss << "|           Instant Meshes 2026 Quality Report            |\n";
    oss << "+============================================================+\n";
    oss << "| Topo Purity:       " << std::fixed << std::setprecision(1)
        << quad_ratio << "% Quads (" << quad_faces << " quads, " << tri_faces << " tris)\n";
    oss << "| Total Vertices:    " << total_vertices 
        << " (" << interior_vertices << " interior, " << (total_vertices - interior_vertices) << " boundary)\n";
    oss << "| Regular Valence:   " << std::fixed << std::setprecision(1)
        << regular_valence_ratio << "% Valence-4 (" << regular_valence_count << " / " << interior_vertices << ")\n";

    oss << "| Valence Histogram: ";
    bool first = true;
    for (const auto &p : valence_histogram) {
        if (!first) oss << ", ";
        oss << "v" << p.first << ":" << p.second;
        first = false;
    }
    oss << "\n";

    oss << "| Edge Lengths:      min=" << std::setprecision(4) << min_edge_length 
        << ", max=" << max_edge_length << ", avg=" << avg_edge_length 
        << " (ratio " << std::setprecision(1) << edge_aspect_ratio << "x)\n";
    oss << "| Scaled Jacobian:   min=" << std::setprecision(3) << min_scaled_jacobian 
        << ", avg=" << avg_scaled_jacobian 
        << " (" << inverted_faces << " inverted)\n";
    oss << "| Manifold Status:   " << (is_manifold ? "Strict 2-Manifold" : "NON-MANIFOLD")
        << " (nm_edges=" << nonmanifold_edges << ", nm_verts=" << nonmanifold_vertices << ")\n";
    oss << "| Boundary Status:   " << (is_watertight ? "Watertight (0 boundary edges)" : (std::to_string(boundary_edges) + " boundary edges")) << "\n";
    oss << "+------------------------------------------------------------+\n";
    oss << "| Quality Gates:     " << (passed_gates ? "[PASSED] Production-Ready Quad Topology" : "[WARNING] Quality Gates Not Met") << "\n";
    oss << "+============================================================+\n";
    return oss.str();
}

void MeshQualityReporter::print_report(const MeshQualityReport &report) {
    std::cout << report.to_string() << std::endl;
}
