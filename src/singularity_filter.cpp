/*
    singularity_filter.cpp: Modern singularity regularization and dipole cancellation filter.
    
    This file is part of the modernization of Instant Meshes.
*/

#include "singularity_filter.h"
#include "field.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <set>
#include <unordered_set>

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

    // Build all candidate pairs within max_distance and sort by shortest distance first
    struct CandidatePair {
        size_t pos_idx;
        size_t neg_idx;
        Float distance;
        bool operator<(const CandidatePair &o) const { return distance < o.distance; }
    };

    std::vector<CandidatePair> candidates;
    for (size_t i = 0; i < pos_faces.size(); ++i) {
        for (size_t j = 0; j < neg_faces.size(); ++j) {
            Float dist = (pos_centers[i] - neg_centers[j]).norm();
            if (dist < max_distance) {
                candidates.push_back({i, j, dist});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end());

    std::set<size_t> matched_pos;
    std::set<size_t> matched_neg;

    for (const auto &cand : candidates) {
        if (matched_pos.count(cand.pos_idx) || matched_neg.count(cand.neg_idx))
            continue;
        matched_pos.insert(cand.pos_idx);
        matched_neg.insert(cand.neg_idx);

        SingularityPair pair;
        pair.face_pos = pos_faces[cand.pos_idx];
        pair.face_neg = neg_faces[cand.neg_idx];
        pair.distance = cand.distance;
        pairs.push_back(pair);
    }

    return pairs;
}

size_t SingularityFilter::regularize_singularities(
    MultiResolutionHierarchy &mRes,
    int rosy,
    Float max_pair_distance,
    bool extrinsic
) {
    if (rosy != 4 || mRes.levels() == 0 || mRes.F().cols() == 0)
        return 0;

    Float scale = mRes.scale();
    if (max_pair_distance <= 0.0f) {
        max_pair_distance = scale * 2.5f;
    }

    std::map<uint32_t, uint32_t> initial_singularities;
    compute_orientation_singularities(mRes, initial_singularities, extrinsic, rosy);

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
    const MatrixXf &CQ = mRes.CQ();
    const VectorXf &CQw = mRes.CQw();
    MatrixXf &Q = mRes.Q();
    const AdjacencyMatrix &adj = mRes.adj();

    auto compat_orient = extrinsic ? compat_orientation_extrinsic_4 : compat_orientation_intrinsic_4;
    auto compat_index  = extrinsic ? compat_orientation_extrinsic_index_4 : compat_orientation_intrinsic_index_4;

    // Build vertex -> incident faces lookup
    std::vector<std::vector<uint32_t>> v2f(V.cols());
    for (uint32_t f = 0; f < (uint32_t)F.cols(); ++f) {
        for (int k = 0; k < 3; ++k) {
            uint32_t v = F(k, f);
            if (v < (uint32_t)V.cols())
                v2f[v].push_back(f);
        }
    }

    auto face_singularity_index = [&](uint32_t f) -> uint32_t {
        int index = 0;
        for (int k = 0; k < 3; ++k) {
            uint32_t i = F(k, f);
            uint32_t j = F(k == 2 ? 0 : (k + 1), f);
            std::pair<int, int> val = compat_index(Q.col(i), N.col(i), Q.col(j), N.col(j));
            index += val.second - val.first;
        }
        index = modulo(index, rosy);
        return (index == 1 || index == rosy - 1) ? (uint32_t)index : 0u;
    };

    auto face_centroid = [&](uint32_t f) -> Vector3f {
        return (V.col(F(0, f)) + V.col(F(1, f)) + V.col(F(2, f))) / 3.0f;
    };

    size_t accepted_dipoles = 0;

    // For each dipole, collect the connecting neighborhood disk D, harmonize orientations
    // from the zero-index boundary ring, relax locally, and accept if singularities strictly decrease.
    for (const auto &pair : dipoles) {
        uint32_t idx_pos = face_singularity_index(pair.face_pos);
        uint32_t idx_neg = face_singularity_index(pair.face_neg);
        if (!((idx_pos == 1 && idx_neg == (uint32_t)(rosy - 1)) ||
              (idx_pos == (uint32_t)(rosy - 1) && idx_neg == 1))) {
            continue;
        }

        Vector3f p_pos = face_centroid(pair.face_pos);
        Vector3f p_neg = face_centroid(pair.face_neg);
        Vector3f center = 0.5f * (p_pos + p_neg);
        Float radius = std::max(pair.distance * 1.25f, scale * 0.75f);
        Float radius_sq = radius * radius;

        // Collect interior vertices D in the dipole zone via bounded BFS
        std::unordered_set<uint32_t> zone_set;
        std::vector<uint32_t> zone_vertices;
        std::queue<uint32_t> bfs;

        for (int k = 0; k < 3; ++k) {
            uint32_t vp = F(k, pair.face_pos);
            uint32_t vn = F(k, pair.face_neg);
            if (zone_set.insert(vp).second) {
                zone_vertices.push_back(vp);
                bfs.push(vp);
            }
            if (zone_set.insert(vn).second) {
                zone_vertices.push_back(vn);
                bfs.push(vn);
            }
        }

        while (!bfs.empty()) {
            uint32_t cur = bfs.front();
            bfs.pop();
            for (const Link *link = adj[cur]; link != adj[cur + 1]; ++link) {
                uint32_t nbr = link->id;
                if (zone_set.count(nbr))
                    continue;
                if ((V.col(nbr) - center).squaredNorm() <= radius_sq) {
                    zone_set.insert(nbr);
                    zone_vertices.push_back(nbr);
                    bfs.push(nbr);
                }
            }
        }

        // Collect boundary ring vertices (neighbors of D outside D) and all incident faces
        std::vector<uint32_t> boundary_vertices;
        std::unordered_set<uint32_t> boundary_set;
        std::unordered_set<uint32_t> local_faces;

        for (uint32_t v : zone_vertices) {
            for (uint32_t f : v2f[v])
                local_faces.insert(f);
            for (const Link *link = adj[v]; link != adj[v + 1]; ++link) {
                uint32_t nbr = link->id;
                if (!zone_set.count(nbr) && boundary_set.insert(nbr).second) {
                    boundary_vertices.push_back(nbr);
                }
            }
        }

        size_t local_sing_before = 0;
        for (uint32_t f : local_faces) {
            if (face_singularity_index(f) != 0)
                local_sing_before++;
        }

        // Backup original Q for vertices in zone_vertices
        std::vector<Vector3f> q_backup(zone_vertices.size());
        for (size_t i = 0; i < zone_vertices.size(); ++i)
            q_backup[i] = Q.col(zone_vertices[i]);

        // Compute coherent reference orientation from the boundary ring
        Vector3f q_ref = Vector3f::Zero();
        Vector3f n_ref = Vector3f::Zero();
        for (uint32_t v : zone_vertices)
            n_ref += N.col(v);
        if (n_ref.squaredNorm() > 1e-8f)
            n_ref.normalize();
        else
            n_ref = N.col(zone_vertices[0]);

        const std::vector<uint32_t> &seed_ring = !boundary_vertices.empty() ? boundary_vertices : zone_vertices;
        q_ref = Q.col(seed_ring[0]);
        q_ref -= n_ref * n_ref.dot(q_ref);
        if (q_ref.squaredNorm() > 1e-8f)
            q_ref.normalize();
        else
            q_ref = Q.col(seed_ring[0]);

        Vector3f accum_q = q_ref;
        Float accum_w = 1.0f;
        for (size_t i = 1; i < seed_ring.size(); ++i) {
            uint32_t b = seed_ring[i];
            auto aligned = compat_orient(accum_q, n_ref, Q.col(b), N.col(b));
            accum_q = aligned.first * accum_w + aligned.second;
            accum_q -= n_ref * n_ref.dot(accum_q);
            Float norm = accum_q.norm();
            if (norm > 1e-6f)
                accum_q /= norm;
            accum_w += 1.0f;
        }

        // Initialize interior vertices to the coherent boundary orientation
        for (uint32_t v : zone_vertices) {
            if (CQw.size() > v && CQw[v] > 0.5f)
                continue;
            Vector3f n_v = N.col(v);
            auto aligned = compat_orient(accum_q, n_ref, Q.col(v), n_v);
            Vector3f q_init = aligned.first - n_v * n_v.dot(aligned.first);
            if (q_init.squaredNorm() > 1e-8f)
                Q.col(v) = q_init.normalized();
        }

        // Perform local Gauss-Seidel relaxation sweeps across the dipole zone
        for (int iter = 0; iter < 16; ++iter) {
            for (uint32_t v : zone_vertices) {
                const Vector3f n_v = N.col(v);
                Vector3f sum = Q.col(v);
                Float weight_sum = 0.0f;

                for (const Link *link = adj[v]; link != adj[v + 1]; ++link) {
                    const uint32_t j = link->id;
                    const Float w = link->weight;
                    if (w <= 0.0f)
                        continue;
                    auto val = compat_orient(sum, n_v, Q.col(j), N.col(j));
                    sum = val.first * weight_sum + val.second * w;
                    sum -= n_v * n_v.dot(sum);
                    weight_sum += w;
                    Float norm = sum.norm();
                    if (norm > RCPOVERFLOW)
                        sum /= norm;
                }

                if (CQw.size() > v && CQw[v] > 0.0f) {
                    Float cw = CQw[v];
                    auto val = compat_orient(sum, n_v, CQ.col(v), n_v);
                    sum = val.first * (1.0f - cw) + val.second * cw;
                    sum -= n_v * n_v.dot(sum);
                    Float norm = sum.norm();
                    if (norm > RCPOVERFLOW)
                        sum /= norm;
                }

                if (weight_sum > 0.0f)
                    Q.col(v) = sum;
            }
        }

        // Verify that local singularities strictly decreased without spawning new ones
        size_t local_sing_after = 0;
        for (uint32_t f : local_faces) {
            if (face_singularity_index(f) != 0)
                local_sing_after++;
        }

        if (local_sing_after < local_sing_before) {
            accepted_dipoles++;
        } else {
            for (size_t i = 0; i < zone_vertices.size(); ++i)
                Q.col(zone_vertices[i]) = q_backup[i];
        }
    }

    if (accepted_dipoles > 0) {
        // Re-freeze integer rotation jumps to match the dipole-cancelled orientation field
        freeze_ivars_orientations(mRes, 0, extrinsic, rosy);
        // Smoothly relax the frozen orientation field across level 0
        optimize_orientations(mRes, 0, extrinsic, rosy, nullptr);
        optimize_orientations(mRes, 0, extrinsic, rosy, nullptr);
        // Propagate regularized solution up the hierarchy
        mRes.propagateSolution(rosy);
    }

    std::map<uint32_t, uint32_t> post_singularities;
    compute_orientation_singularities(mRes, post_singularities, extrinsic, rosy);

    size_t cancelled = (before_count > post_singularities.size())
        ? (before_count - post_singularities.size()) / 2
        : 0;

    std::cout << "[SingularityFilter] Regularization complete. Singularities reduced from "
              << before_count << " to " << post_singularities.size() << "." << std::endl;

    return cancelled;
}
