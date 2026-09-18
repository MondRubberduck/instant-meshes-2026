/*
    contour_guide.h: Advanced Contour and Topology Flow Guidance System.
    
    This file is part of the modernization of Instant Meshes.
*/

#pragma once

#include "common.h"
#include "bvh.h"
#include "hierarchy.h"
#include <vector>

struct GuideContour {
    std::vector<Vector3f> points;   // 3D world space points along the stroke
    bool is_edge_loop = true;       // True: edge loop (orientation + position snapping), False: flow comb (orientation only)
    bool is_closed = false;         // True: closed loop (e.g. eye, mouth, limb ring), False: open line
    Float influence_radius = 0.0f;  // Influence radius (0 = auto-calculate based on scale)
    Float weight = 1.0f;            // Constraint importance weight in [0, 1]
    bool mirror_x = false;          // Mirror contour across X=0 symmetry plane
};

struct ProjectedContourPoint {
    Vector3f p;      // Projected point on surface
    Vector3f n;      // Surface normal at point
    Vector3f t;      // Tangent direction along curve
    uint32_t face;   // Face index
};

class ContourGuideSystem {
public:
    ContourGuideSystem();

    // Add a contour to the guidance system
    void add_contour(const GuideContour &contour);

    // Clear all contours
    void clear();

    // Number of active contours
    size_t count() const { return m_contours.size(); }

    // Read access to contours
    const std::vector<GuideContour>& contours() const { return m_contours; }

    // Enable / disable global bilateral X-axis symmetry reflection
    void set_mirror_x(bool enable) { m_mirror_x = enable; }
    bool mirror_x() const { return m_mirror_x; }

    /**
     * Project, smooth, and apply all contours onto the MultiResolutionHierarchy.
     * 
     * @param mRes Hierarchy containing V, N, F, CQ, CO, etc.
     * @param bvh Ray-tracing bounding volume hierarchy for fast projection
     * @param rosy Rotational symmetry (default: 4 for quads)
     * @param posy Positional symmetry (default: 4 for quads)
     */
    void apply_to_hierarchy(
        MultiResolutionHierarchy &mRes,
        const BVH *bvh,
        int rosy = 4,
        int posy = 4
    );

private:
    std::vector<GuideContour> m_contours;
    bool m_mirror_x = false;

    // Resample and project a raw contour onto the mesh surface via BVH
    std::vector<ProjectedContourPoint> project_and_resample(
        const GuideContour &contour,
        const MatrixXu &F,
        const MatrixXf &V,
        const MatrixXf &N,
        const BVH *bvh,
        Float step_size
    );
};
