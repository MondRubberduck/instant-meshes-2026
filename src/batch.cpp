/*
    batch.cpp -- command line interface to Instant Meshes
    
    This file is part of the modernization of Instant Meshes.
*/

#include "batch.h"
#include "retopo_engine.h"
#include <fstream>
#include <sstream>

static std::vector<GuideContour> load_contours_from_obj(const std::string &path) {
    std::vector<GuideContour> contours;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Batch] Warning: could not open contour file: " << path << std::endl;
        return contours;
    }

    std::vector<Vector3f> vertices;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        if (!(iss >> token))
            continue;

        if (token == "v") {
            Float x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3f(x, y, z));
        } else if (token == "l") {
            GuideContour contour;
            int idx;
            std::vector<int> poly_indices;
            while (iss >> idx) {
                // 1-based indexing in OBJ
                if (idx > 0 && idx <= (int)vertices.size()) {
                    poly_indices.push_back(idx - 1);
                }
            }

            if (poly_indices.size() >= 2) {
                for (int v_idx : poly_indices) {
                    contour.points.push_back(vertices[v_idx]);
                }
                // If first and last vertex match, it's a closed contour loop
                if (poly_indices.front() == poly_indices.back() && poly_indices.size() > 2) {
                    contour.is_closed = true;
                    contour.points.pop_back(); // remove duplicate endpoint
                }
                contour.is_edge_loop = true;
                contour.weight = 1.0f;
                contours.push_back(contour);
            }
        }
    }

    std::cout << "[Batch] Loaded " << contours.size() 
              << " guide contours from " << path << std::endl;
    return contours;
}

void batch_process(const std::string &input, const std::string &output,
                   int rosy, int posy, Float scale, int face_count,
                   int vertex_count, Float creaseAngle, bool extrinsic,
                   bool align_to_boundaries, int smooth_iter, int knn_points,
                   bool pure_quad, bool deterministic,
                   Float adaptivity, const std::string &contour_file) {
    cout << endl;
    cout << "Running Instant Meshes 2026 batch mode:" << endl;
    cout << "   Input file             = " << input << endl;
    cout << "   Output file            = " << output << endl;
    cout << "   Rotation symmetry type = " << rosy << endl;
    cout << "   Position symmetry type = " << (posy == 3 ? 6 : posy) << endl;
    cout << "   Target vertex count    = " << (vertex_count > 0 ? std::to_string(vertex_count) : "auto") << endl;
    cout << "   Target face count      = " << (face_count > 0 ? std::to_string(face_count) : "auto") << endl;
    cout << "   Curvature adaptivity   = " << adaptivity << endl;
    if (!contour_file.empty())
        cout << "   Contour file           = " << contour_file << endl;
    cout << "   Output mode            = " << (pure_quad ? "pure quad mesh" : "quad-dominant mesh") << endl;
    cout << endl;

    RetopoEngine engine;
    if (!engine.load_file(input)) {
        cerr << "Error: could not load input mesh: " << input << endl;
        return;
    }

    if (!contour_file.empty()) {
        std::vector<GuideContour> contours = load_contours_from_obj(contour_file);
        for (const auto &c : contours)
            engine.add_contour(c);
    }

    RetopoSettings settings;
    settings.rosy = rosy;
    settings.posy = posy;
    settings.scale = scale;
    settings.target_face_count = face_count;
    settings.target_vertex_count = vertex_count;
    settings.crease_angle = creaseAngle;
    settings.align_to_boundaries = align_to_boundaries;
    settings.smooth_iterations = smooth_iter;
    settings.knn_points = knn_points;
    settings.pure_quad = pure_quad;
    settings.deterministic = deterministic;
    settings.adaptivity = adaptivity;
    settings.filter_singularities = true;
    settings.intrinsic = !extrinsic;

    RetopoOutput result = engine.execute(settings);

    if (!result.success) {
        cerr << "Retopology failed: " << result.error_message << endl;
        return;
    }

    if (!RetopoEngine::save_mesh(output, result)) {
        cerr << "Error: failed to write output mesh to " << output << endl;
    }
}
