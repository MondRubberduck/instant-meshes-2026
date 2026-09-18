/*
    batch.h -- command line interface to Instant Meshes
    
    This file is part of the modernization of Instant Meshes.
*/

#pragma once

#include "common.h"
#include <string>

extern void batch_process(const std::string &input, const std::string &output,
                          int rosy, int posy, Float scale, int face_count,
                          int vertex_count, Float creaseAngle, bool extrinsic,
                          bool align_to_boundaries, int smooth_iter,
                          int knn_points, bool pure_quad, bool deterministic,
                          Float adaptivity = 0.0f,
                          const std::string &contour_file = "");
