/*
    singularity_filter.h: Modern singularity regularization and dipole cancellation filter.
    
    Inspired by QuadriFlow (SIGGRAPH 2018), this filter detects adjacent opposite-sign
    singularities (+1/4 and -1/4) and cancels them to eliminate spiral loops and irregular poles.
*/

#pragma once

#include "common.h"
#include "hierarchy.h"
#include <map>
#include <vector>

struct SingularityPair {
    uint32_t face_pos;  // +1/4 singularity face index
    uint32_t face_neg;  // -1/4 singularity face index
    Float distance;     // Distance between them
};

class SingularityFilter {
public:
    SingularityFilter();

    /**
     * Detect and cancel opposite-sign singularity dipoles on the orientation field.
     * 
     * @param mRes Hierarchy containing orientation field Q, normals N, faces F, vertices V.
     * @param rosy Rotational symmetry (default: 4 for quads)
     * @param max_pair_distance Maximum distance to consider for dipole cancellation (<= 0 for auto: 2.5 * scale)
     * @return Number of cancelled dipole pairs
     */
    static size_t regularize_singularities(
        MultiResolutionHierarchy &mRes,
        int rosy = 4,
        Float max_pair_distance = -1.0f
    );

    /**
     * Find dipole pairs of opposite singularities within search radius.
     */
    static std::vector<SingularityPair> find_dipoles(
        const MultiResolutionHierarchy &mRes,
        const std::map<uint32_t, uint32_t> &singularities,
        Float max_distance
    );
};
