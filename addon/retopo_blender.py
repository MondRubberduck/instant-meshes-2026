bl_info = {
    "name": "InstantMeshes2026 Retopology",
    "author": "Antigravity / Instant Meshes 2026 Team (original by Jakob et al.)",
    "version": (2026, 1, 0),
    "blender": (3, 0, 0),
    "location": "View3D > Sidebar > Retopo",
    "description": "InstantMeshes 2026 field-aligned quad retopology with adaptive density and contour flow guidance",
    "category": "Mesh",
}

import bpy
import numpy as np
import os
import sys
import subprocess
import tempfile

# Try importing compiled C++ python module
try:
    from . import pyretopo
    HAS_PYRETOPO = True
except ImportError:
    try:
        import pyretopo
        HAS_PYRETOPO = True
    except ImportError:
        addon_dir = os.path.dirname(os.path.abspath(__file__))
        if addon_dir not in sys.path:
            sys.path.insert(0, addon_dir)
        try:
            import pyretopo
            HAS_PYRETOPO = True
        except ImportError:
            HAS_PYRETOPO = False


class RETOPO_Properties(bpy.types.PropertyGroup):
    target_faces: bpy.props.IntProperty(
        name="Target Faces",
        description="Approximate number of quad faces in retopologized output",
        default=5000,
        min=16,
        max=1000000
    )
    adaptivity: bpy.props.FloatProperty(
        name="Curvature Adaptivity",
        description="Higher values place smaller quads in curved areas (ears, eyes, creases) and larger quads in flat areas",
        default=0.35,
        min=0.0,
        max=1.0
    )
    pure_quad: bpy.props.BoolProperty(
        name="Pure Quad Mesh",
        description="Force 100% pure quad mesh (applies 1 subdivision pass if necessary)",
        default=True
    )
    mirror_x: bpy.props.BoolProperty(
        name="Mirror X Symmetry",
        description="Reflect flow contours across the X=0 plane for bilateral symmetry (characters/heads/props)",
        default=False
    )
    use_annotations: bpy.props.BoolProperty(
        name="Use Viewport Annotations",
        description="Include strokes drawn using Blender's Annotate tool as topology flow guides",
        default=True
    )
    filter_singularities: bpy.props.BoolProperty(
        name="Singularity Filter",
        description="QuadriFlow-inspired dipole cancellation to eliminate spiral loops and irregular poles",
        default=True
    )
    align_to_boundaries: bpy.props.BoolProperty(
        name="Align to Boundaries",
        description="Align edge loops with open mesh boundaries",
        default=True
    )
    crease_angle: bpy.props.FloatProperty(
        name="Crease Angle",
        description="Angle threshold for sharp CAD features (-1 = disabled/smooth)",
        default=-1.0,
        min=-1.0,
        max=180.0
    )
    smooth_iterations: bpy.props.IntProperty(
        name="Smoothing Iterations",
        description="Number of Laplacian smoothing and surface ray-reprojection steps",
        default=2,
        min=0,
        max=10
    )
    intrinsic: bpy.props.BoolProperty(
        name="Intrinsic Cotangent Laplacian",
        description="Compute discrete cotangent Laplacian weights for robust field solving on irregular/scan meshes",
        default=True
    )
    has_report: bpy.props.BoolProperty(default=False)
    last_quad_ratio: bpy.props.StringProperty(default="")
    last_valence_ratio: bpy.props.StringProperty(default="")
    last_manifold_status: bpy.props.StringProperty(default="")
    last_jacobian: bpy.props.StringProperty(default="")
    last_gate_status: bpy.props.StringProperty(default="")


class RETOPO_OT_draw_guide(bpy.types.Operator):
    bl_idname = "mesh.instant_retopo_draw_guide"
    bl_label = "Draw Guide (Annotate)"
    bl_description = "Switch to 3D Viewport Annotate tool to sketch flow contours directly on the surface"

    def execute(self, context):
        try:
            bpy.ops.wm.tool_set_by_id(name="builtin.annotate")
            self.report({'INFO'}, "Annotate tool active: Draw topology guide lines on the mesh surface.")
        except Exception as e:
            self.report({'WARNING'}, f"Could not set annotate tool: {e}")
        return {'FINISHED'}


class RETOPO_OT_clear_guides(bpy.types.Operator):
    bl_idname = "mesh.instant_retopo_clear_guides"
    bl_label = "Clear Guides"
    bl_description = "Clear all 3D Viewport Annotations"

    def execute(self, context):
        cleared = 0
        for gp in bpy.data.grease_pencils:
            for layer in gp.layers:
                for frame in layer.frames:
                    frame.strokes.clear()
                    cleared += 1
        self.report({'INFO'}, f"Cleared {cleared} annotation layers/frames.")
        return {'FINISHED'}


class RETOPO_OT_retopologize(bpy.types.Operator):
    bl_idname = "mesh.instant_retopo_execute"
    bl_label = "Remesh with InstantMeshes 2026"
    bl_description = "Execute InstantMeshes 2026 quad retopology with contour flow constraints"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        return context.active_object and context.active_object.type == 'MESH'

    def execute(self, context):
        obj = context.active_object
        props = context.scene.retopo_props

        self.report({'INFO'}, f"Starting InstantMeshes 2026 retopology on {obj.name} (Target: {props.target_faces} faces)...")

        # 1. Extract evaluated triangulated mesh data
        depsgraph = context.evaluated_depsgraph_get()
        eval_obj = obj.evaluated_get(depsgraph)
        mesh = eval_obj.to_mesh()

        # Triangulate temporarily for robust field calculation
        import bmesh
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.to_mesh(mesh)
        bm.free()

        num_verts = len(mesh.vertices)
        num_faces = len(mesh.polygons)

        if num_verts == 0 or num_faces == 0:
            self.report({'ERROR'}, "Mesh has no geometry!")
            eval_obj.to_mesh_clear()
            return {'CANCELLED'}

        # World matrix
        world_mat = obj.matrix_world

        vertices = np.zeros((num_verts, 3), dtype=np.float32)
        for i, v in enumerate(mesh.vertices):
            co = world_mat @ v.co
            vertices[i] = [co.x, co.y, co.z]

        faces = np.zeros((num_faces, 3), dtype=np.uint32)
        for i, f in enumerate(mesh.polygons):
            faces[i] = [f.vertices[0], f.vertices[1], f.vertices[2]]

        eval_obj.to_mesh_clear()

        # 2. Extract contour curves from Curve objects in scene named "Contour*" or selected
        contours = []
        for curve_obj in context.scene.objects:
            if curve_obj.type == 'CURVE' and ("contour" in curve_obj.name.lower() or curve_obj.select_get()):
                c_mat = curve_obj.matrix_world
                for spline in curve_obj.data.splines:
                    pts = []
                    if spline.type == 'BEZIER':
                        for bp in spline.bezier_points:
                            wco = c_mat @ bp.co
                            pts.append([wco.x, wco.y, wco.z])
                    else:
                        for p in spline.points:
                            wco = c_mat @ p.co.xyz
                            pts.append([wco.x, wco.y, wco.z])

                    if len(pts) >= 2:
                        contour_data = {
                            "points": pts,
                            "is_edge_loop": True,
                            "is_closed": spline.use_cyclic_u,
                            "weight": 1.0,
                            "mirror_x": props.mirror_x
                        }
                        contours.append(contour_data)

        # B: Viewport Annotations / Grease Pencil
        if props.use_annotations:
            for gp in bpy.data.grease_pencils:
                for layer in gp.layers:
                    for frame in layer.frames:
                        for stroke in frame.strokes:
                            if len(stroke.points) >= 2:
                                pts = [[p.co.x, p.co.y, p.co.z] for p in stroke.points]
                                p_first = stroke.points[0].co
                                p_last = stroke.points[-1].co
                                is_closed = (p_first - p_last).length < 0.05
                                contour_data = {
                                    "points": pts,
                                    "is_edge_loop": True,
                                    "is_closed": is_closed,
                                    "weight": 1.0,
                                    "mirror_x": props.mirror_x
                                }
                                contours.append(contour_data)

        # 3. Run retopology engine
        out_verts = None
        out_faces = None

        if HAS_PYRETOPO:
            # High-performance in-memory execution via pybind11
            guide_contours = []
            for c in contours:
                gc = pyretopo.GuideContour()
                gc.points = c["points"]
                gc.is_edge_loop = c["is_edge_loop"]
                gc.is_closed = c["is_closed"]
                gc.weight = c["weight"]
                gc.mirror_x = c["mirror_x"]
                guide_contours.append(gc)

            result = pyretopo.retopologize(
                vertices,
                faces,
                target_faces=props.target_faces,
                target_vertices=-1,
                adaptivity=props.adaptivity,
                contours=guide_contours,
                crease_angle=props.crease_angle,
                align_to_boundaries=props.align_to_boundaries,
                pure_quad=props.pure_quad,
                smooth_iterations=props.smooth_iterations,
                filter_singularities=props.filter_singularities,
                mirror_x=props.mirror_x,
                intrinsic=props.intrinsic
            )

            if not result.success:
                self.report({'ERROR'}, f"Retopology failed: {result.error_message}")
                return {'CANCELLED'}

            qr = result.quality_report
            props.has_report = True
            props.last_quad_ratio = f"{qr.quad_ratio:.1f}% ({qr.quad_faces} quads)"
            props.last_valence_ratio = f"{qr.regular_valence_ratio:.1f}% regular v4"
            props.last_manifold_status = "Strict 2-Manifold" if qr.is_manifold else "Non-Manifold"
            props.last_jacobian = f"min J = {qr.min_scaled_jacobian:.2f} ({qr.inverted_faces} inverted)"
            props.last_gate_status = "PASSED" if qr.passed_gates else "WARNING"

            out_verts = result.vertices
            out_faces = result.faces
        else:
            # Fallback to CLI executable
            self.report({'WARNING'}, "pyretopo C-extension not loaded in Blender. Falling back to InstantMeshes2026CLI executable...")
            with tempfile.TemporaryDirectory() as tmpdir:
                in_path = os.path.join(tmpdir, "input.obj")
                out_path = os.path.join(tmpdir, "output.obj")
                contour_path = os.path.join(tmpdir, "contours.obj")

                # Export OBJ
                with open(in_path, "w") as f:
                    for v in vertices:
                        f.write(f"v {v[0]} {v[1]} {v[2]}\n")
                    for p in faces:
                        f.write(f"f {p[0]+1} {p[1]+1} {p[2]+1}\n")

                cli_candidates = [
                    "InstantMeshes2026CLI.exe" if sys.platform == "win32" else "InstantMeshes2026CLI",
                    "InstantMeshesCLI.exe" if sys.platform == "win32" else "InstantMeshesCLI",
                ]
                cmd_exe = "InstantMeshes2026CLI"
                for cli_name in cli_candidates:
                    local_cli = os.path.join(os.path.dirname(os.path.abspath(__file__)), cli_name)
                    app_cli = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "InstantMeshes_App", cli_name)
                    if os.path.exists(local_cli):
                        cmd_exe = local_cli
                        break
                    elif os.path.exists(app_cli):
                        cmd_exe = app_cli
                        break

                cmd = [
                    cmd_exe,
                    "-f", str(props.target_faces),
                    "-a", str(props.adaptivity),
                    "-o", out_path,
                ]
                if props.pure_quad:
                    pass
                else:
                    cmd.append("-D")

                if contours:
                    with open(contour_path, "w") as f:
                        idx_offset = 1
                        for c in contours:
                            for pt in c["points"]:
                                f.write(f"v {pt[0]} {pt[1]} {pt[2]}\n")
                            indices = list(range(idx_offset, idx_offset + len(c["points"])))
                            if c["is_closed"]:
                                indices.append(idx_offset)
                            f.write("l " + " ".join(map(str, indices)) + "\n")
                            idx_offset += len(c["points"])
                    cmd.extend(["--contour", contour_path])

                cmd.append(in_path)

                try:
                    subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    # Load result
                    bpy.ops.wm.obj_import(filepath=out_path)
                    new_obj = context.selected_objects[0]
                    new_obj.name = f"{obj.name}_Retopo"
                    self.report({'INFO'}, "Retopology completed successfully via CLI!")
                    return {'FINISHED'}
                except Exception as e:
                    self.report({'ERROR'}, f"CLI execution failed: {e}")
                    return {'CANCELLED'}

        # 4. Construct new Blender Mesh from NumPy arrays
        new_mesh = bpy.data.meshes.new(f"{obj.name}_Retopo_Mesh")
        new_mesh.from_pydata(out_verts.tolist(), [], out_faces.tolist())
        new_mesh.update()

        new_obj = bpy.data.objects.new(f"{obj.name}_Retopo", new_mesh)
        context.collection.objects.link(new_obj)

        # Select new object
        bpy.ops.object.select_all(action='DESELECT')
        new_obj.select_set(True)
        context.view_layer.objects.active = new_obj

        # Apply smooth shading
        for poly in new_mesh.polygons:
            poly.use_smooth = True

        self.report({'INFO'}, f"Retopology finished: {len(new_mesh.polygons)} quads generated.")
        return {'FINISHED'}


class VIEW3D_PT_retopo_panel(bpy.types.Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Retopo'
    bl_label = 'InstantMeshes 2026'

    def draw(self, context):
        layout = self.layout
        props = context.scene.retopo_props

        # Budget settings
        col = layout.column(align=True)
        col.label(text="Mesh Budget:", icon='MESH_DATA')
        col.prop(props, "target_faces")
        col.prop(props, "adaptivity", slider=True)

        col = layout.column(align=True)
        col.prop(props, "pure_quad")
        col.prop(props, "mirror_x")
        col.prop(props, "filter_singularities")
        col.prop(props, "intrinsic")
        col.prop(props, "align_to_boundaries")

        col = layout.column(align=True)
        col.prop(props, "smooth_iterations")
        col.prop(props, "crease_angle")

        layout.separator()
        box = layout.box()
        box.label(text="Contour Guides (Topology Flow)", icon='CURVE_PATH')
        box.prop(props, "use_annotations")

        row = box.row(align=True)
        row.operator("mesh.instant_retopo_draw_guide", icon='GREASEPENCIL', text="Draw Guide")
        row.operator("mesh.instant_retopo_clear_guides", icon='X', text="Clear")

        layout.separator()
        layout.operator("mesh.instant_retopo_execute", icon='MOD_REMESH')

        if props.has_report:
            layout.separator()
            qbox = layout.box()
            qbox.label(text=f"Quality Gates: [{props.last_gate_status}]", icon='CHECKMARK' if props.last_gate_status == "PASSED" else 'ERROR')
            qbox.label(text=f"• Purity: {props.last_quad_ratio}")
            qbox.label(text=f"• Regularity: {props.last_valence_ratio}")
            qbox.label(text=f"• Topology: {props.last_manifold_status}")
            qbox.label(text=f"• Distortion: {props.last_jacobian}")


classes = (
    RETOPO_Properties,
    RETOPO_OT_draw_guide,
    RETOPO_OT_clear_guides,
    RETOPO_OT_retopologize,
    VIEW3D_PT_retopo_panel,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.retopo_props = bpy.props.PointerProperty(type=RETOPO_Properties)


def uninstall():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
    del bpy.types.Scene.retopo_props


if __name__ == "__main__":
    register()
