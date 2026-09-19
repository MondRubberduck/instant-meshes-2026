/*
    bindings.cpp: pybind11 Python bindings for Instant Meshes modernization.
    
    Exposes high-performance retopology to Python and Blender.
*/

#include <pybind11/pybind11.h>
#include <pybind11/eigen.h>
#include <pybind11/stl.h>

#include "../src/retopo_engine.h"

namespace py = pybind11;

int nprocs = -1;

PYBIND11_MODULE(pyretopo, m) {
    m.doc() = "InstantMeshes2026 Headless Retopology Engine (pyretopo)";

    py::class_<GuideContour>(m, "GuideContour")
        .def(py::init<>())
        .def_readwrite("points", &GuideContour::points)
        .def_readwrite("is_edge_loop", &GuideContour::is_edge_loop)
        .def_readwrite("is_closed", &GuideContour::is_closed)
        .def_readwrite("influence_radius", &GuideContour::influence_radius)
        .def_readwrite("weight", &GuideContour::weight)
        .def_readwrite("mirror_x", &GuideContour::mirror_x);

    py::class_<MeshQualityReport>(m, "MeshQualityReport")
        .def(py::init<>())
        .def_readonly("total_faces", &MeshQualityReport::total_faces)
        .def_readonly("quad_faces", &MeshQualityReport::quad_faces)
        .def_readonly("tri_faces", &MeshQualityReport::tri_faces)
        .def_readonly("ngon_faces", &MeshQualityReport::ngon_faces)
        .def_readonly("quad_ratio", &MeshQualityReport::quad_ratio)
        .def_readonly("total_vertices", &MeshQualityReport::total_vertices)
        .def_readonly("interior_vertices", &MeshQualityReport::interior_vertices)
        .def_readonly("regular_valence_count", &MeshQualityReport::regular_valence_count)
        .def_readonly("regular_valence_ratio", &MeshQualityReport::regular_valence_ratio)
        .def_readonly("valence_histogram", &MeshQualityReport::valence_histogram)
        .def_readonly("total_edges", &MeshQualityReport::total_edges)
        .def_readonly("min_edge_length", &MeshQualityReport::min_edge_length)
        .def_readonly("max_edge_length", &MeshQualityReport::max_edge_length)
        .def_readonly("avg_edge_length", &MeshQualityReport::avg_edge_length)
        .def_readonly("edge_aspect_ratio", &MeshQualityReport::edge_aspect_ratio)
        .def_readonly("min_scaled_jacobian", &MeshQualityReport::min_scaled_jacobian)
        .def_readonly("avg_scaled_jacobian", &MeshQualityReport::avg_scaled_jacobian)
        .def_readonly("inverted_faces", &MeshQualityReport::inverted_faces)
        .def_readonly("boundary_edges", &MeshQualityReport::boundary_edges)
        .def_readonly("nonmanifold_edges", &MeshQualityReport::nonmanifold_edges)
        .def_readonly("nonmanifold_vertices", &MeshQualityReport::nonmanifold_vertices)
        .def_readonly("is_watertight", &MeshQualityReport::is_watertight)
        .def_readonly("is_manifold", &MeshQualityReport::is_manifold)
        .def_readonly("passed_gates", &MeshQualityReport::passed_gates)
        .def("summary", &MeshQualityReport::to_string)
        .def("__repr__", &MeshQualityReport::to_string)
        .def("to_dict", [](const MeshQualityReport &r) {
            py::dict d;
            d["total_faces"] = r.total_faces;
            d["quad_faces"] = r.quad_faces;
            d["tri_faces"] = r.tri_faces;
            d["ngon_faces"] = r.ngon_faces;
            d["quad_ratio"] = r.quad_ratio;
            d["total_vertices"] = r.total_vertices;
            d["interior_vertices"] = r.interior_vertices;
            d["regular_valence_count"] = r.regular_valence_count;
            d["regular_valence_ratio"] = r.regular_valence_ratio;
            d["valence_histogram"] = r.valence_histogram;
            d["min_edge_length"] = r.min_edge_length;
            d["max_edge_length"] = r.max_edge_length;
            d["avg_edge_length"] = r.avg_edge_length;
            d["edge_aspect_ratio"] = r.edge_aspect_ratio;
            d["min_scaled_jacobian"] = r.min_scaled_jacobian;
            d["avg_scaled_jacobian"] = r.avg_scaled_jacobian;
            d["inverted_faces"] = r.inverted_faces;
            d["boundary_edges"] = r.boundary_edges;
            d["nonmanifold_edges"] = r.nonmanifold_edges;
            d["nonmanifold_vertices"] = r.nonmanifold_vertices;
            d["is_watertight"] = r.is_watertight;
            d["is_manifold"] = r.is_manifold;
            d["passed_gates"] = r.passed_gates;
            return d;
        });

    py::class_<RetopoSettings>(m, "RetopoSettings")
        .def(py::init<>())
        .def_readwrite("rosy", &RetopoSettings::rosy)
        .def_readwrite("posy", &RetopoSettings::posy)
        .def_readwrite("target_vertex_count", &RetopoSettings::target_vertex_count)
        .def_readwrite("target_face_count", &RetopoSettings::target_face_count)
        .def_readwrite("scale", &RetopoSettings::scale)
        .def_readwrite("adaptivity", &RetopoSettings::adaptivity)
        .def_readwrite("crease_angle", &RetopoSettings::crease_angle)
        .def_readwrite("align_to_boundaries", &RetopoSettings::align_to_boundaries)
        .def_readwrite("pure_quad", &RetopoSettings::pure_quad)
        .def_readwrite("deterministic", &RetopoSettings::deterministic)
        .def_readwrite("smooth_iterations", &RetopoSettings::smooth_iterations)
        .def_readwrite("filter_singularities", &RetopoSettings::filter_singularities)
        .def_readwrite("mirror_x", &RetopoSettings::mirror_x)
        .def_readwrite("intrinsic", &RetopoSettings::intrinsic);

    py::class_<RetopoOutput>(m, "RetopoOutput")
        .def(py::init<>())
        .def_property_readonly("vertices", [](const RetopoOutput &o) { return MatrixXf(o.V.transpose()); })
        .def_property_readonly("faces", [](const RetopoOutput &o) { return MatrixXu(o.F.transpose()); })
        .def_property_readonly("normals", [](const RetopoOutput &o) { return MatrixXf(o.N.transpose()); })
        .def_readonly("vertex_count", &RetopoOutput::vertex_count)
        .def_readonly("face_count", &RetopoOutput::face_count)
        .def_readonly("edge_length", &RetopoOutput::edge_length)
        .def_readonly("elapsed_time_ms", &RetopoOutput::elapsed_time_ms)
        .def_readonly("success", &RetopoOutput::success)
        .def_readonly("error_message", &RetopoOutput::error_message)
        .def_readonly("quality_report", &RetopoOutput::quality_report);

    py::class_<RetopoEngine>(m, "RetopoEngine")
        .def(py::init<>())
        .def("set_mesh", [](RetopoEngine &engine, const MatrixXf &vertices, const MatrixXu &faces) {
            engine.set_mesh(faces.transpose(), vertices.transpose());
        }, py::arg("vertices"), py::arg("faces"), "Set mesh with (N, 3) vertices and (M, 3) triangle faces.")
        .def("load_file", &RetopoEngine::load_file, py::arg("filename"))
        .def("add_contour", &RetopoEngine::add_contour, py::arg("contour"))
        .def("add_contour_points", [](RetopoEngine &engine, const std::vector<Vector3f> &points, bool is_edge_loop, bool is_closed, Float weight) {
            engine.add_contour_points(points, is_edge_loop, is_closed, weight);
        }, py::arg("points"), py::arg("is_edge_loop") = true, py::arg("is_closed") = false, py::arg("weight") = 1.0f)
        .def("clear_contours", &RetopoEngine::clear_contours)
        .def("execute", [](RetopoEngine &engine, const RetopoSettings &settings) {
            return engine.execute(settings);
        }, py::arg("settings"));

    m.def("retopologize", [](
        const MatrixXf &vertices, 
        const MatrixXu &faces,
        int target_faces,
        int target_vertices,
        Float adaptivity,
        const std::vector<GuideContour> &contours,
        Float crease_angle,
        bool align_to_boundaries,
        bool pure_quad,
        int smooth_iterations,
        bool filter_singularities,
        bool mirror_x,
        bool intrinsic
    ) -> RetopoOutput {
        RetopoEngine engine;
        engine.set_mesh(faces.transpose(), vertices.transpose());

        for (const auto &c : contours)
            engine.add_contour(c);

        RetopoSettings settings;
        settings.target_face_count = target_faces;
        settings.target_vertex_count = target_vertices;
        settings.adaptivity = adaptivity;
        settings.crease_angle = crease_angle;
        settings.align_to_boundaries = align_to_boundaries;
        settings.pure_quad = pure_quad;
        settings.smooth_iterations = smooth_iterations;
        settings.filter_singularities = filter_singularities;
        settings.mirror_x = mirror_x;
        settings.intrinsic = intrinsic;

        return engine.execute(settings);
    }, 
    py::arg("vertices"),
    py::arg("faces"),
    py::arg("target_faces") = 5000,
    py::arg("target_vertices") = -1,
    py::arg("adaptivity") = 0.0f,
    py::arg("contours") = std::vector<GuideContour>(),
    py::arg("crease_angle") = -1.0f,
    py::arg("align_to_boundaries") = true,
    py::arg("pure_quad") = true,
    py::arg("smooth_iterations") = 2,
    py::arg("filter_singularities") = true,
    py::arg("mirror_x") = false,
    py::arg("intrinsic") = false,
    "High-level retopology function returning RetopoOutput directly from input vertex and face arrays."
    );
}
