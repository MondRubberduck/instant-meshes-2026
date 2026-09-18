/*
    extraction_sanitizer.cpp: BSD-native QEx-style Extraction Sanitization & Topological Hardening.
    
    This file is part of the modernization of Instant Meshes.
*/

#include "extraction_sanitizer.h"
#include "dedge.h"
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <algorithm>

size_t ExtractionSanitizer::sanitize_coarse_quads(
    MatrixXu &F,
    MatrixXf &O,
    MatrixXf &N,
    std::set<uint32_t> &crease
) {
    if (F.cols() == 0 || O.cols() == 0)
        return 0;

    size_t issues_resolved = 0;
    uint32_t nF = (uint32_t)F.cols();
    uint32_t nV = (uint32_t)O.cols();

    // 1. Remove duplicate/collapsed vertices within individual faces
    for (uint32_t f = 0; f < nF; ++f) {
        if (F(0, f) == INVALID)
            continue;

        int deg = (F.rows() == 4 && F(2, f) == F(3, f)) ? 3 : (int)F.rows();
        std::vector<uint32_t> verts;
        for (int i = 0; i < deg; ++i) {
            uint32_t v = F(i, f);
            if (verts.empty() || verts.back() != v)
                verts.push_back(v);
        }
        // Check wrap-around
        if (verts.size() > 1 && verts.front() == verts.back())
            verts.pop_back();

        if (deg == 4 && verts.size() == 4) {
            // Check for bow-tie (opposite vertices match)
            if (verts[0] == verts[2] || verts[1] == verts[3]) {
                // Bow-tie quad: degenerate, collapse or invalidate
                F.col(f).setConstant(INVALID);
                issues_resolved++;
                continue;
            }
        }

        if (verts.size() < 3) {
            // Collapsed face (line or point)
            F.col(f).setConstant(INVALID);
            issues_resolved++;
            continue;
        } else if (verts.size() == 3 && F.rows() == 4) {
            // Reduced to triangle
            F.col(f) << verts[0], verts[1], verts[2], verts[2];
            issues_resolved++;
        } else if (verts.size() == 4 && F.rows() == 4) {
            F.col(f) << verts[0], verts[1], verts[2], verts[3];
        }
    }

    // 2. Remove duplicate overlapping faces
    std::set<std::vector<uint32_t>> unique_faces;
    for (uint32_t f = 0; f < nF; ++f) {
        if (F(0, f) == INVALID)
            continue;
        int deg = (F.rows() == 4 && F(2, f) == F(3, f)) ? 3 : (int)F.rows();
        std::vector<uint32_t> sorted_verts(deg);
        for (int i = 0; i < deg; ++i)
            sorted_verts[i] = F(i, f);
        std::sort(sorted_verts.begin(), sorted_verts.end());

        if (unique_faces.find(sorted_verts) != unique_faces.end()) {
            F.col(f).setConstant(INVALID);
            issues_resolved++;
        } else {
            unique_faces.insert(sorted_verts);
        }
    }

    // 3. Realign face windings (detect inverted quads compared to vertex normals)
    for (uint32_t f = 0; f < nF; ++f) {
        if (F(0, f) == INVALID)
            continue;
        int deg = (F.rows() == 4 && F(2, f) == F(3, f)) ? 3 : (int)F.rows();
        if (deg == 4) {
            uint32_t v0 = F(0, f), v1 = F(1, f), v2 = F(2, f), v3 = F(3, f);
            if (v0 >= nV || v1 >= nV || v2 >= nV || v3 >= nV)
                continue;
            Vector3f p0 = O.col(v0), p1 = O.col(v1), p2 = O.col(v2), p3 = O.col(v3);
            Vector3f fn = ((p1 - p0).cross(p3 - p0) + (p3 - p2).cross(p1 - p2));
            Vector3f vn = (N.col(v0) + N.col(v1) + N.col(v2) + N.col(v3));
            if (fn.dot(vn) < -1e-4f) {
                // Inverted winding: reverse to restore positive orientation
                F.col(f) << v0, v3, v2, v1;
                issues_resolved++;
            }
        }
    }

    // 4. Manifold star enforcement: split pinched vertices into 2-manifold disks
    // Build vertex -> incident faces mapping
    std::vector<std::vector<uint32_t>> v2f(nV);
    for (uint32_t f = 0; f < nF; ++f) {
        if (F(0, f) == INVALID)
            continue;
        int deg = (F.rows() == 4 && F(2, f) == F(3, f)) ? 3 : (int)F.rows();
        for (int i = 0; i < deg; ++i) {
            uint32_t v = F(i, f);
            if (v < nV)
                v2f[v].push_back(f);
        }
    }

    std::vector<Vector3f> new_V_pos;
    std::vector<Vector3f> new_V_norm;
    std::vector<bool> new_V_crease;

    for (uint32_t v = 0; v < nV; ++v) {
        const auto &inc_faces = v2f[v];
        if (inc_faces.size() <= 1)
            continue;

        // Build adjacency among incident faces sharing an edge with vertex v
        std::map<uint32_t, std::vector<uint32_t>> face_adj;
        for (size_t a = 0; a < inc_faces.size(); ++a) {
            uint32_t fa = inc_faces[a];
            int deg_a = (F.rows() == 4 && F(2, fa) == F(3, fa)) ? 3 : (int)F.rows();
            // Find neighbors of v in fa
            uint32_t prev_a = INVALID, next_a = INVALID;
            for (int i = 0; i < deg_a; ++i) {
                if (F(i, fa) == v) {
                    prev_a = F((i + deg_a - 1) % deg_a, fa);
                    next_a = F((i + 1) % deg_a, fa);
                    break;
                }
            }

            for (size_t b = a + 1; b < inc_faces.size(); ++b) {
                uint32_t fb = inc_faces[b];
                int deg_b = (F.rows() == 4 && F(2, fb) == F(3, fb)) ? 3 : (int)F.rows();
                uint32_t prev_b = INVALID, next_b = INVALID;
                for (int i = 0; i < deg_b; ++i) {
                    if (F(i, fb) == v) {
                        prev_b = F((i + deg_b - 1) % deg_b, fb);
                        next_b = F((i + 1) % deg_b, fb);
                        break;
                    }
                }

                // If fa and fb share an edge containing v
                if ((prev_a == next_b && prev_a != INVALID) || (next_a == prev_b && next_a != INVALID) ||
                    (prev_a == prev_b && prev_a != INVALID) || (next_a == next_b && next_a != INVALID)) {
                    face_adj[fa].push_back(fb);
                    face_adj[fb].push_back(fa);
                }
            }
        }

        // Find connected components among incident faces
        std::set<uint32_t> visited_faces;
        std::vector<std::vector<uint32_t>> components;

        for (uint32_t f_start : inc_faces) {
            if (visited_faces.find(f_start) != visited_faces.end())
                continue;

            std::vector<uint32_t> comp;
            std::vector<uint32_t> queue = { f_start };
            visited_faces.insert(f_start);

            while (!queue.empty()) {
                uint32_t cur = queue.back();
                queue.pop_back();
                comp.push_back(cur);

                for (uint32_t nbr : face_adj[cur]) {
                    if (visited_faces.find(nbr) == visited_faces.end()) {
                        visited_faces.insert(nbr);
                        queue.push_back(nbr);
                    }
                }
            }
            components.push_back(comp);
        }

        // If vertex v has multiple disconnected fans, it's a non-manifold pinch vertex!
        if (components.size() > 1) {
            // First component stays with vertex v.
            // For components 1..n-1, assign a new vertex instance.
            bool is_crease_vert = (crease.find(v) != crease.end());
            for (size_t c = 1; c < components.size(); ++c) {
                uint32_t v_new = nV + (uint32_t)new_V_pos.size();
                new_V_pos.push_back(O.col(v));
                new_V_norm.push_back(N.col(v));
                new_V_crease.push_back(is_crease_vert);

                for (uint32_t f_comp : components[c]) {
                    int deg = (F.rows() == 4 && F(2, f_comp) == F(3, f_comp)) ? 3 : (int)F.rows();
                    for (int i = 0; i < deg; ++i) {
                        if (F(i, f_comp) == v) {
                            F(i, f_comp) = v_new;
                        }
                    }
                }
                issues_resolved++;
            }
        }
    }

    // Append newly split vertices if any
    if (!new_V_pos.empty()) {
        uint32_t old_cols = (uint32_t)O.cols();
        uint32_t new_total = old_cols + (uint32_t)new_V_pos.size();
        O.conservativeResize(3, new_total);
        N.conservativeResize(3, new_total);
        for (size_t i = 0; i < new_V_pos.size(); ++i) {
            uint32_t idx = old_cols + (uint32_t)i;
            O.col(idx) = new_V_pos[i];
            N.col(idx) = new_V_norm[i];
            if (new_V_crease[i])
                crease.insert(idx);
        }
    }

    // Compact faces: remove INVALID rows
    uint32_t valid_faces = 0;
    for (uint32_t f = 0; f < nF; ++f) {
        if (F(0, f) != INVALID) {
            if (valid_faces != f)
                F.col(valid_faces) = F.col(f);
            valid_faces++;
        }
    }
    if (valid_faces != nF)
        F.conservativeResize(F.rows(), valid_faces);

    if (issues_resolved > 0) {
        std::cout << "[ExtractionSanitizer] Coarse quad sanitization resolved " 
                  << issues_resolved << " topological anomalies (fold-overs, pinches, degenerate edges)." 
                  << std::endl;
    }

    return issues_resolved;
}

size_t ExtractionSanitizer::sanitize_final_mesh(
    MatrixXu &F,
    MatrixXf &O,
    MatrixXf &N,
    MatrixXf &Nf
) {
    if (F.cols() == 0 || O.cols() == 0)
        return 0;

    size_t cleaned = 0;
    uint32_t nF = (uint32_t)F.cols();
    uint32_t nV = (uint32_t)O.cols();

    // Check for zero-area or collapsed faces in final mesh
    for (uint32_t f = 0; f < nF; ++f) {
        if (F(0, f) == INVALID)
            continue;

        int deg = (F.rows() == 4 && F(2, f) == F(3, f)) ? 3 : (int)F.rows();
        bool degenerate = false;
        for (int i = 0; i < deg; ++i) {
            uint32_t v0 = F(i, f);
            uint32_t v1 = F((i + 1) % deg, f);
            if (v0 >= nV || v1 >= nV || v0 == v1) {
                degenerate = true;
                break;
            }
        }
        if (deg == 4) {
            if (F(0, f) == F(2, f) || F(1, f) == F(3, f))
                degenerate = true;
        }

        if (degenerate) {
            F.col(f).setConstant(INVALID);
            cleaned++;
        }
    }

    // Check and repair inverted/folded quads
    for (uint32_t f = 0; f < nF; ++f) {
        if (F(0, f) == INVALID)
            continue;
        if (F.rows() == 4 && F(2, f) != F(3, f)) {
            uint32_t idx[4] = { F(0, f), F(1, f), F(2, f), F(3, f) };
            if (idx[0] >= nV || idx[1] >= nV || idx[2] >= nV || idx[3] >= nV)
                continue;
            Vector3f p[4] = { O.col(idx[0]), O.col(idx[1]), O.col(idx[2]), O.col(idx[3]) };
            Vector3f avg_norm = (N.col(idx[0]) + N.col(idx[1]) + N.col(idx[2]) + N.col(idx[3])).normalized();
            Vector3f fn = ((p[1] - p[0]).cross(p[3] - p[0]) + (p[3] - p[2]).cross(p[1] - p[2]));
            if (fn.norm() > 1e-6f)
                fn.normalize();

            // 1. If overall normal is opposite to vertex normal, flip winding
            if (fn.dot(avg_norm) < 0.0f) {
                std::swap(idx[1], idx[3]);
                F.col(f) << idx[0], idx[1], idx[2], idx[3];
                std::swap(p[1], p[3]);
                fn = -fn;
                cleaned++;
            }

            // 2. Check each corner for reflex/inverted angles and untangle
            for (int iters = 0; iters < 3; ++iters) {
                Float min_j = 1.0f;
                int worst_corner = -1;
                for (int i = 0; i < 4; ++i) {
                    Vector3f e_in = p[i] - p[(i + 3) % 4];
                    Vector3f e_out = p[(i + 1) % 4] - p[i];
                    Float l_in = e_in.norm(), l_out = e_out.norm();
                    if (l_in > 1e-6f && l_out > 1e-6f) {
                        Float j = e_in.cross(e_out).dot(avg_norm) / (l_in * l_out);
                        if (j < min_j) {
                            min_j = j;
                            worst_corner = i;
                        }
                    }
                }

                if (min_j <= 0.01f && worst_corner >= 0) {
                    // Untangle reflex corner by moving towards convex side of chord
                    uint32_t opp = (worst_corner + 2) % 4;
                    uint32_t prev = (worst_corner + 3) % 4;
                    uint32_t next = (worst_corner + 1) % 4;
                    Vector3f chord_mid = (p[prev] + p[next]) * 0.5f;
                    Vector3f outward_dir = (chord_mid - p[opp]);
                    if (outward_dir.norm() > 1e-6f) {
                        Vector3f target_pos = chord_mid + outward_dir * 0.3f;
                        p[worst_corner] = target_pos;
                        O.col(idx[worst_corner]) = target_pos;
                        cleaned++;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
        }
    }

    // Compact F and Nf
    uint32_t valid_faces = 0;
    for (uint32_t f = 0; f < nF; ++f) {
        if (F(0, f) != INVALID) {
            if (valid_faces != f) {
                F.col(valid_faces) = F.col(f);
                if (Nf.cols() == nF)
                    Nf.col(valid_faces) = Nf.col(f);
            }
            valid_faces++;
        }
    }
    if (valid_faces != nF) {
        F.conservativeResize(F.rows(), valid_faces);
        if (Nf.cols() == nF)
            Nf.conservativeResize(Nf.rows(), valid_faces);
    }

    return cleaned;
}
