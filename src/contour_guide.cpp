/*
    contour_guide.cpp: Advanced Contour and Topology Flow Guidance System.
    
    This file is part of the modernization of Instant Meshes.
*/

#include "contour_guide.h"
#include "field.h"
#include <cmath>
#include <iostream>
#include <set>

ContourGuideSystem::ContourGuideSystem() {
}

void ContourGuideSystem::add_contour(const GuideContour &contour) {
    if (contour.points.size() >= 2) {
        m_contours.push_back(contour);
    }
}

void ContourGuideSystem::clear() {
    m_contours.clear();
}

// Precision barycentric point-to-triangle projection (Ericson, RTCD Section 5.1.5)
static Vector3f closest_point_on_triangle(
    const Vector3f &p,
    const Vector3f &a,
    const Vector3f &b,
    const Vector3f &c,
    Vector3f &bary
) {
    Vector3f ab = b - a;
    Vector3f ac = c - a;
    Vector3f ap = p - a;
    Float d1 = ab.dot(ap);
    Float d2 = ac.dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        bary = Vector3f(1.0f, 0.0f, 0.0f);
        return a;
    }

    Vector3f bp = p - b;
    Float d3 = ab.dot(bp);
    Float d4 = ac.dot(bp);
    if (d3 >= 0.0f && d4 <= d3) {
        bary = Vector3f(0.0f, 1.0f, 0.0f);
        return b;
    }

    Float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        Float v = d1 / (d1 - d3);
        bary = Vector3f(1.0f - v, v, 0.0f);
        return a + v * ab;
    }

    Vector3f cp = p - c;
    Float d5 = ab.dot(cp);
    Float d6 = ac.dot(cp);
    if (d6 >= 0.0f && d5 <= d6) {
        bary = Vector3f(0.0f, 0.0f, 1.0f);
        return c;
    }

    Float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        Float w = d2 / (d2 - d6);
        bary = Vector3f(1.0f - w, 0.0f, w);
        return a + w * ac;
    }

    Float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        Float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        bary = Vector3f(0.0f, 1.0f - w, w);
        return b + w * (c - b);
    }

    Float denom = 1.0f / (va + vb + vc);
    Float v = vb * denom;
    Float w = vc * denom;
    Float u = 1.0f - v - w;
    bary = Vector3f(u, v, w);
    return a + ab * v + ac * w;
}

std::vector<ProjectedContourPoint> ContourGuideSystem::project_and_resample(
    const GuideContour &contour,
    const MatrixXu &F,
    const MatrixXf &V,
    const MatrixXf &N,
    const BVH *bvh,
    Float step_size
) {
    std::vector<ProjectedContourPoint> result;
    if (contour.points.size() < 2)
        return result;

    // 1. Resample raw points along polyline with uniform step size
    std::vector<Vector3f> resampled;
    resampled.push_back(contour.points.front());

    Float current_dist = 0.0f;
    size_t num_pts = contour.points.size();
    size_t segments = contour.is_closed ? num_pts : (num_pts - 1);

    for (size_t i = 0; i < segments; ++i) {
        Vector3f p0 = contour.points[i];
        Vector3f p1 = contour.points[(i + 1) % num_pts];
        Float seg_len = (p1 - p0).norm();
        if (seg_len < 1e-6f)
            continue;

        Float traveled = 0.0f;
        while (traveled + (step_size - current_dist) <= seg_len) {
            traveled += (step_size - current_dist);
            Float alpha = traveled / seg_len;
            resampled.push_back(p0 * (1.0f - alpha) + p1 * alpha);
            current_dist = 0.0f;
        }
        current_dist += (seg_len - traveled);
    }

    if (!contour.is_closed && (resampled.back() - contour.points.back()).norm() > 1e-4f) {
        resampled.push_back(contour.points.back());
    }

    if (resampled.size() < 2)
        return result;

    // 2. Project each resampled point to mesh surface with exact barycentric precision
    size_t total_resampled = resampled.size();
    result.reserve(total_resampled);

    for (size_t i = 0; i < total_resampled; ++i) {
        Vector3f query_p = resampled[i];
        Float radius = step_size * 5.0f;
        uint32_t nearest_face = bvh ? bvh->findNearest(query_p, radius) : 0;

        Vector3f surf_p = query_p;
        Vector3f surf_n = Vector3f::UnitY();

        if (nearest_face < F.cols()) {
            uint32_t i0 = F(0, nearest_face), i1 = F(1, nearest_face), i2 = F(2, nearest_face);
            Vector3f a = V.col(i0), b = V.col(i1), c = V.col(i2);
            Vector3f n0 = N.col(i0), n1 = N.col(i1), n2 = N.col(i2);
            Vector3f bary;
            surf_p = closest_point_on_triangle(query_p, a, b, c, bary);
            surf_n = (bary.x() * n0 + bary.y() * n1 + bary.z() * n2);
            if (surf_n.squaredNorm() > 1e-6f)
                surf_n.normalize();
            else
                surf_n = (b - a).cross(c - a).normalized();
        }

        // Tangent vector
        Vector3f tangent;
        if (i == 0) {
            if (contour.is_closed) {
                tangent = resampled[1] - resampled[total_resampled - 1];
            } else {
                tangent = resampled[1] - resampled[0];
            }
        } else if (i == total_resampled - 1) {
            if (contour.is_closed) {
                tangent = resampled[0] - resampled[total_resampled - 2];
            } else {
                tangent = resampled[total_resampled - 1] - resampled[total_resampled - 2];
            }
        } else {
            tangent = resampled[i + 1] - resampled[i - 1];
        }

        if (tangent.norm() > 1e-6f)
            tangent.normalize();
        else
            tangent = Vector3f::UnitX();

        // Project tangent to surface normal plane
        tangent = (tangent - surf_n * surf_n.dot(tangent));
        if (tangent.norm() > 1e-6f)
            tangent.normalize();

        ProjectedContourPoint cp;
        cp.p = surf_p;
        cp.n = surf_n;
        cp.t = tangent;
        cp.face = nearest_face;
        result.push_back(cp);
    }

    return result;
}

void ContourGuideSystem::apply_to_hierarchy(
    MultiResolutionHierarchy &mRes,
    const BVH *bvh,
    int rosy,
    int posy
) {
    if (m_contours.empty() || mRes.levels() == 0)
        return;

    const MatrixXu &F = mRes.F();
    const MatrixXf &V = mRes.V();
    const MatrixXf &N = mRes.N();
    uint32_t n_vertices = V.cols();

    MatrixXf &CQ = mRes.CQ();
    VectorXf &CQw = mRes.CQw();
    MatrixXf &CO = mRes.CO();
    VectorXf &COw = mRes.COw();

    Float scale = mRes.scale();
    Float default_step = (scale > 0.0f) ? (scale * 0.5f) : 0.05f;

    auto compat_orient = (rosy == 2) ? compat_orientation_extrinsic_2 :
                         (rosy == 4) ? compat_orientation_extrinsic_4 :
                         compat_orientation_extrinsic_6;

    // Expand contours with bilateral mirror symmetry (X-axis reflection) if requested
    std::vector<GuideContour> active_contours = m_contours;
    size_t base_count = m_contours.size();
    for (size_t i = 0; i < base_count; ++i) {
        if (m_mirror_x || m_contours[i].mirror_x) {
            GuideContour mirrored = m_contours[i];
            for (auto &pt : mirrored.points) {
                pt.x() = -pt.x();
            }
            active_contours.push_back(mirrored);
        }
    }

    std::cout << "[ContourGuideSystem] Applying " << active_contours.size() 
              << " topology flow contours (including bilateral symmetry) to fields..." << std::endl;

    for (const auto &contour : active_contours) {
        Float influence_radius = (contour.influence_radius > 0.0f) 
            ? contour.influence_radius 
            : (scale * 1.5f);

        Float sigma = influence_radius * 0.5f;
        Float two_sigma_sq = 2.0f * sigma * sigma;

        std::vector<ProjectedContourPoint> projected = project_and_resample(
            contour, F, V, N, bvh, default_step
        );

        if (projected.empty())
            continue;

        for (const auto &pt : projected) {
            std::set<uint32_t> nearby_vertices;

            if (bvh && F.cols() > 0) {
                std::vector<uint32_t> nearby_faces;
                bvh->findNearestWithRadius(pt.p, influence_radius, nearby_faces);
                for (uint32_t f : nearby_faces) {
                    if (f < F.cols()) {
                        nearby_vertices.insert(F(0, f));
                        nearby_vertices.insert(F(1, f));
                        nearby_vertices.insert(F(2, f));
                    }
                }
            } else {
                for (uint32_t v = 0; v < n_vertices; ++v) {
                    if ((V.col(v) - pt.p).norm() <= influence_radius)
                        nearby_vertices.insert(v);
                }
            }

            for (uint32_t v : nearby_vertices) {
                if (v >= n_vertices)
                    continue;

                Vector3f vert_pos = V.col(v);
                Vector3f vert_normal = N.col(v);
                Float dist = (vert_pos - pt.p).norm();

                Float spatial_w = contour.weight * std::exp(- (dist * dist) / two_sigma_sq);
                if (spatial_w < 1e-4f)
                    continue;

                // Project contour tangent onto vertex normal plane
                Vector3f local_tangent = pt.t - vert_normal * (pt.t.dot(vert_normal));
                if (local_tangent.norm() < 1e-6f)
                    continue;
                local_tangent.normalize();

                // Merge into CQ with rotation symmetry compatibility
                if (CQw[v] <= 1e-6f) {
                    CQ.col(v) = local_tangent;
                    CQw[v] = spatial_w;
                } else {
                    auto aligned = compat_orient(CQ.col(v), vert_normal, local_tangent, vert_normal);
                    Vector3f merged = aligned.first * CQw[v] + aligned.second * spatial_w;
                    merged = merged - vert_normal * (merged.dot(vert_normal));
                    if (merged.norm() > 1e-6f) {
                        CQ.col(v) = merged.normalized();
                        CQw[v] = std::min(1.0f, CQw[v] + spatial_w);
                    }
                }

                // If edge loop contour, snap position coordinate for vertices close to the stroke
                if (contour.is_edge_loop && dist <= (scale * 0.75f)) {
                    Float pos_w = contour.weight * (1.0f - dist / (scale * 0.75f));
                    if (COw[v] < pos_w) {
                        CO.col(v) = pt.p;
                        COw[v] = pos_w;
                    }
                }
            }
        }
    }

    // Propagate all applied constraints up through the multi-resolution hierarchy
    mRes.propagateConstraints(rosy, posy);
}
