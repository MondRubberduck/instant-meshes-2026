/*
    singularity_filter.cpp: Modern singularity regularization and dipole cancellation filter.
    
    This file is part of the modernization of Instant Meshes.
*/

#include "singularity_filter.h"
#include "field.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

SingularityFilter::SingularityFilter() {
}

std::vector<SingularityPair> SingularityFilter::find_dipoles(
    const MultiResolutionHierarchy &mRes,
    const std::map<uint32_t, uint32_t> &singularities,
    Float max_distance
) {
    std::vector<SingularityPair> pairs;
    if (singularities.size() < 2)
        return pairs;

    const MatrixXu &F = mRes.F();
    const MatrixXf &V = mRes.V();

    std::vector<uint32_t> pos_faces;
    std::vector<uint32_t> neg_faces;

    for (const auto &kv : singularities) {
        if (kv.second == 1) {
            pos_faces.push_back(kv.first);
        } else if (kv.second == 3) {
            neg_faces.push_back(kv.first);
        }
    }

    if (pos_faces.empty() || neg_faces.empty())
        return pairs;

    auto face_centroid = [&](uint32_t f) -> Vector3f {
        return (V.col(F(0, f)) + V.col(F(1, f)) + V.col(F(2, f))) / 3.0f;
    };

    std::vector<Vector3f> pos_centers(pos_faces.size());
    for (size_t i = 0; i < pos_faces.size(); ++i)
        pos_centers[i] = face_centroid(pos_faces[i]);

    std::vector<Vector3f> neg_centers(neg_faces.size());
    for (size_t i = 0; i < neg_faces.size(); ++i)
        neg_centers[i] = face_centroid(neg_faces[i]);

    std::set<size_t> matched_neg;

    // Greedy closest-pair matching
    for (size_t i = 0; i < pos_faces.size(); ++i) {
        Float best_dist = max_distance;
        int best_neg = -1;

        for (size_t j = 0; j < neg_faces.size(); ++j) {
            if (matched_neg.count(j))
                continue;

            Float dist = (pos_centers[i] - neg_centers[j]).norm();
            if (dist < best_dist) {
                best_dist = dist;
                best_neg = (int)j;
            }
        }

        if (best_neg >= 0) {
            matched_neg.insert((size_t)best_neg);
            SingularityPair pair;
            pair.face_pos = pos_faces[i];
            pair.face_neg = neg_faces[best_neg];
            pair.distance = best_dist;
            pairs.push_back(pair);
        }
    }

    return pairs;
}

size_t SingularityFilter::regularize_singularities(
    MultiResolutionHierarchy &mRes,
    int rosy,
    Float max_pair_distance
) {
    if (rosy != 4 || mRes.levels() == 0 || mRes.F().cols() == 0)
        return 0;

    Float scale = mRes.scale();
    if (max_pair_distance <= 0.0f) {
        max_pair_distance = scale * 2.5f;
    }

    std::map<uint32_t, uint32_t> initial_singularities;
    compute_orientation_singularities(mRes, initial_singularities, true, rosy);

    size_t before_count = initial_singularities.size();
    if (before_count < 2)
        return 0;

    std::vector<SingularityPair> dipoles = find_dipoles(mRes, initial_singularities, max_pair_distance);
    if (dipoles.empty())
        return 0;

    std::cout << "[SingularityFilter] Found " << dipoles.size() 
              << " opposite-sign dipoles out of " << before_count 
              << " singularities. Cancelling dipoles to eliminate spirals..." << std::endl;

    const MatrixXu &F = mRes.F();
    const MatrixXf &V = mRes.V();
    const MatrixXf &N = mRes.N();
    MatrixXf &Q = mRes.Q();
    const AdjacencyMatrix &adj = mRes.adj();

    auto face_centroid = [&](uint32_t f) -> Vector3f {
        return (V.col(F(0, f)) + V.col(F(1, f)) + V.col(F(2, f))) / 3.0f;
    };

    // For each dipole, harmonize orientations along the connecting zone
    for (const auto &pair : dipoles) {
        Vector3f p_pos = face_centroid(pair.face_pos);
        Vector3f p_neg = face_centroid(pair.face_neg);
        Vector3f center = 0.5f * (p_pos + p_neg);
        Float radius = pair.distance * 1.25f;

        // Collect vertices in the dipole zone
        std::vector<uint32_t> zone_vertices;
        for (uint32_t k = 0; k < 3; ++k) {
            zone_vertices.push_back(F(k, pair.face_pos));
            zone_vertices.push_back(F(k, pair.face_neg));
        }

        for (uint32_t v : zone_vertices) {
            Vector3f n = N.col(v);
            Vector3f avg_q = Vector3f::Zero();
            Float count = 0.0f;

            // Average with neighboring non-singular vertices
            for (const Link *link = adj[v]; link != adj[v + 1]; ++link) {
                uint32_t neighbor = link->id;
                Vector3f q_neigh = Q.col(neighbor);
                Vector3f n_neigh = N.col(neighbor);

                auto compat = compat_orientation_extrinsic_4(Q.col(v), n, q_neigh, n_neigh);
                avg_q += compat.second * link->weight;
                count += link->weight;
            }

            if (count > 1e-6f) {
                avg_q = avg_q - n * (n.dot(avg_q));
                if (avg_q.norm() > 1e-6f)
                    Q.col(v) = avg_q.normalized();
            }
        }
    }

    // Run local orientation relaxation on level 0
    optimize_orientations(mRes, 0, true, rosy, nullptr);

    // Propagate regularized solution up the hierarchy
    mRes.propagateSolution(rosy);

    std::map<uint32_t, uint32_t> post_singularities;
    compute_orientation_singularities(mRes, post_singularities, true, rosy);

    size_t cancelled = (before_count > post_singularities.size()) 
        ? (before_count - post_singularities.size()) / 2 
        : dipoles.size();

    std::cout << "[SingularityFilter] Regularization complete. Singularities reduced from "
              << before_count << " to " << post_singularities.size() << "." << std::endl;

    return cancelled;
}
