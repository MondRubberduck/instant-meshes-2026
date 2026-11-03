import sys
import os
import tempfile

# Locate the pyretopo module: prefer fresh CMake build outputs, fall back to
# the prebuilt module shipped in blender_extension/.
_HERE = os.path.dirname(os.path.abspath(__file__))
for candidate in reversed((
    os.path.join(_HERE, "build", "python", "Release"),
    os.path.join(_HERE, "build", "python"),
    os.path.join(_HERE, "build_py", "Release"),
    os.path.join(_HERE, "build_py"),
    os.path.join(_HERE, "blender_extension"),
)):
    if os.path.isdir(candidate):
        sys.path.insert(0, candidate)

import pyretopo
import numpy as np

print("Successfully imported pyretopo!")
print("Module doc:", pyretopo.__doc__)

# Load torus mesh
vertices = []
faces = []
with open(os.path.join(_HERE, "torus.obj"), "r") as f:
    for line in f:
        tokens = line.strip().split()
        if not tokens:
            continue
        if tokens[0] == "v":
            vertices.append([float(tokens[1]), float(tokens[2]), float(tokens[3])])
        elif tokens[0] == "f":
            faces.append([int(tokens[1])-1, int(tokens[2])-1, int(tokens[3])-1])

V = np.array(vertices, dtype=np.float32)
F = np.array(faces, dtype=np.uint32)

print(f"Loaded input mesh: V={V.shape}, F={F.shape}")

# Create guide contour
contour = pyretopo.GuideContour()
contour.points = [[1.4 * np.cos(u), 1.4 * np.sin(u), 0.0] for u in np.linspace(0, 2*np.pi, 32, endpoint=False)]
contour.is_edge_loop = True
contour.is_closed = True
contour.weight = 1.0

print("Running in-memory pyretopo.retopologize with target_faces=500, adaptivity=0.6, and 1 contour guide...")
result = pyretopo.retopologize(
    vertices=V,
    faces=F,
    target_faces=500,
    target_vertices=-1,
    adaptivity=0.6,
    contours=[contour],
    crease_angle=-1.0,
    align_to_boundaries=True,
    pure_quad=True,
    smooth_iterations=2,
    filter_singularities=True,
    deterministic=True
)

print(f"Success: {result.success}")
print(f"Output vertices shape: {result.vertices.shape}")
print(f"Output faces shape: {result.faces.shape}")
print(f"Output face_normals shape: {result.face_normals.shape}")
print(f"Generated {result.face_count} quad faces in {result.elapsed_time_ms:.1f} ms.")

assert result.success == True
assert result.face_normals.shape == (result.face_count, 3)
# Target was 500 faces. Verify polycount accuracy (should be ~500, not 1150+):
error_pct = abs(result.face_count - 500) / 500.0 * 100.0
print(f"Target count: 500, Actual count: {result.face_count} (Error: {error_pct:.1f}%)")
assert 380 <= result.face_count <= 650, f"Face count {result.face_count} deviates too far from target 500!"

print("\nRunning test 2: Quad-Dominant mode with target_faces=350...")
result_dom = pyretopo.retopologize(
    vertices=V,
    faces=F,
    target_faces=350,
    pure_quad=False,
    filter_singularities=True,
    deterministic=True
)
print(f"Dominant mode faces: {result_dom.face_count} (Target: 350)")
assert result_dom.success == True
assert 260 <= result_dom.face_count <= 520

print("\nRunning test 3: Bilateral mirror symmetry test...")
contour_sym = pyretopo.GuideContour()
contour_sym.points = [[0.8 + 0.3 * np.cos(u), 0.0, 0.3 * np.sin(u)] for u in np.linspace(0, 2*np.pi, 16, endpoint=False)]
contour_sym.is_edge_loop = True
contour_sym.mirror_x = True

result_sym = pyretopo.retopologize(
    vertices=V,
    faces=F,
    target_faces=400,
    contours=[contour_sym],
    mirror_x=True,
    deterministic=True
)
print(f"Symmetry retopo faces: {result_sym.face_count} in {result_sym.elapsed_time_ms:.1f} ms")
assert result_sym.success == True

print("\nRunning test 4: Quality Gates and Metrics test...")
qr = result.quality_report
print("Quality summary:\n", qr.summary())
assert qr.total_faces == result.face_count
assert qr.quad_ratio == 100.0, f"Expected 100% quads, got {qr.quad_ratio}%"
assert qr.is_manifold == True, "Expected strict 2-manifold mesh"
assert qr.is_watertight == True, "Expected watertight mesh"
assert qr.passed_gates == True, "Expected Quality Gates [PASSED]"
qr_dict = qr.to_dict()
assert isinstance(qr_dict, dict)
assert "regular_valence_ratio" in qr_dict
print(f"Quality Gate metrics: quad_ratio={qr_dict['quad_ratio']}%, regular_v4={qr_dict['regular_valence_ratio']:.1f}%, min_jacobian={qr_dict['min_scaled_jacobian']:.2f}")

print("\nRunning test 5: Intrinsic cotangent Laplacian mode...")
result_intrinsic = pyretopo.retopologize(
    vertices=V,
    faces=F,
    target_faces=500,
    intrinsic=True,
    deterministic=True
)
assert result_intrinsic.success == True
print(f"Intrinsic retopo: {result_intrinsic.face_count} faces, quad_ratio={result_intrinsic.quality_report.quad_ratio}%, regular_v4={result_intrinsic.quality_report.regular_valence_ratio:.1f}%")
assert result_intrinsic.quality_report.is_manifold == True

print("\nRunning test 6: Curvature adaptivity (adaptivity=0.0 vs adaptivity=0.8)...")
res_uniform = pyretopo.retopologize(
    vertices=V,
    faces=F,
    target_faces=500,
    adaptivity=0.0,
    deterministic=True
)
res_adaptive = pyretopo.retopologize(
    vertices=V,
    faces=F,
    target_faces=500,
    adaptivity=0.8,
    deterministic=True
)
assert res_uniform.success and res_adaptive.success
# Verify that adaptivity actually alters the generated mesh geometry/edge distribution
def edge_length_cv(verts, quads):
    edges = []
    for k in range(4):
        p0 = verts[quads[:, k]]
        p1 = verts[quads[:, (k + 1) % 4]]
        edges.append(np.linalg.norm(p1 - p0, axis=1))
    all_edges = np.concatenate(edges)
    return float(np.std(all_edges) / np.mean(all_edges))

cv_uniform = edge_length_cv(res_uniform.vertices, res_uniform.faces)
cv_adaptive = edge_length_cv(res_adaptive.vertices, res_adaptive.faces)
print(f"Uniform edge CV: {cv_uniform:.4f}, Adaptive edge CV: {cv_adaptive:.4f}")
assert not np.array_equal(res_uniform.vertices, res_adaptive.vertices), "Expected adaptivity=0.8 to produce a different mesh than adaptivity=0.0!"

print("\nRunning test 7: Sharp crease angle preservation without crash...")
res_crease = pyretopo.retopologize(
    vertices=V,
    faces=F,
    target_faces=600,
    crease_angle=30.0,
    deterministic=True
)
assert res_crease.success == True
print(f"Crease retopo: {res_crease.face_count} faces, normals shape={res_crease.normals.shape}")

print("\nRunning test 8: OBJ N-gon loading and RetopoEngine.save_mesh...")
with tempfile.TemporaryDirectory() as tmpdir:
    ngon_obj = os.path.join(tmpdir, "ngon_patch.obj")
    out_obj = os.path.join(tmpdir, "out_saved.obj")
    # Write a simple mesh containing a pentagon and a quad on top of torus vertices
    with open(ngon_obj, "w") as f:
        with open(os.path.join(_HERE, "torus.obj"), "r") as src:
            lines = src.readlines()
        v_lines = [l for l in lines if l.startswith("v ")]
        f_lines = [l for l in lines if l.startswith("f ")]
        for l in v_lines:
            f.write(l)
        # Write most triangle faces, plus one synthetic pentagon face at the end
        for l in f_lines[:-2]:
            f.write(l)
        f.write("f 1 2 3 4 5\n")

    eng = pyretopo.RetopoEngine()
    loaded = eng.load_file(ngon_obj)
    assert loaded == True, "Expected load_file to succeed on OBJ with pentagon"
    s = pyretopo.RetopoSettings()
    s.target_face_count = 400
    s.knn_points = 10
    s.deterministic = True
    out = eng.execute(s)
    assert out.success == True
    eng.save_mesh(out_obj, out)
    assert os.path.isfile(out_obj) and os.path.getsize(out_obj) > 100
    print(f"N-gon load + execute + save_mesh succeeded ({os.path.getsize(out_obj)} bytes written).")

print("\n>>> ALL PYTHON INTEGRATION TESTS PASSED! <<<")
