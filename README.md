# Instant Meshes 2026

[![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE.txt)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Python](https://img.shields.io/badge/Python-3.13-green.svg)](https://python.org)
[![Blender](https://img.shields.io/badge/Blender-4.2%20%7C%205.x%20LTS-orange.svg)](https://blender.org)
[![Donate with PayPal](https://img.shields.io/badge/Donate-PayPal-00457C?logo=paypal&logoColor=white)](https://www.paypal.com/donate/?hosted_button_id=5XKG8WLLRWZ44)
[![Support on Patreon](https://img.shields.io/badge/Support-Patreon-FF424D?logo=patreon&logoColor=white)](https://www.patreon.com/NGallist)

**Instant Meshes 2026** is a modernized, production-hardened fork of the seminal field-aligned retopology software. It transforms the original algorithm into a decoupled C++17 library, native Python 3.13 extension (`pyretopo`), and a high-performance in-memory Blender 5.x extension.

---

### Homage & Attribution

This project is built directly upon the groundbreaking academic research:

> **Instant Field-Aligned Meshes**  
> Wenzel Jakob, Marco Tarini, Daniele Panozzo, and Olga Sorkine-Hornung  
> *ACM Transactions on Graphics (Proceedings of SIGGRAPH Asia 2015)*  
> [Project Page](https://igl.ethz.ch/projects/instant-meshes/) | [Paper PDF](https://igl.ethz.ch/projects/instant-meshes/instant-meshes-SA-2015-jakob-et-al.pdf) | [Original Repository](https://github.com/wjakob/instant-meshes)

We express immense gratitude to **Wenzel Jakob** and the ETH Zurich Interactive Geometry Lab. Their field-aligned meshing formulation remains one of the fastest and most elegant geometry processing algorithms ever devised.

---

### Key Improvements (2026 Modernization)

| Feature | Original (2015) | Instant Meshes 2026 |
| :--- | :--- | :--- |
| **Exact Polycount Control** | Abstract scale slider only | **Adaptive bisection solver** targeting exact face counts (e.g. 500, 1500, 5000) |
| **Blender Integration** | External CLI scripts via temporary files | **Native in-memory extension** (Blender 4.2+ / 5.x) running in <50ms |
| **Python Bindings** | None | **Zero-copy C++ extension** (`pyretopo`) supporting NumPy arrays directly |
| **Architecture** | Monolithic GUI executable | **Decoupled C++17 static library** (`retopo_core`) + headless CLI |
| **Mesh Conditioning** | Uniform graph Laplacians (sensitive to skinny tris) | **Clamped intrinsic cotangent Laplacian** $[\epsilon, 100.0]$ |
| **File Formats** | Legacy `.obj`, `.ply`, `.aln` only | **Native Autodesk FBX** (`.fbx` binary & ASCII via `ufbx`), `.obj`, `.ply`, `.aln` |
| **Feature Curves** | Edge shrinking during smoothing | **1D tangential curve relaxation** preserving sharp silhouettes and guide loops |
| **Extraction Sanitization**| Basic edge collapsing (often produced pinch vertices) | **BSD-native topological untangling** (chord relaxation, 2-manifold disk splitting) |
| **Quality Verification** | None (visual inspection only) | **Automated Quality Gates** reporting Quad Purity %, Valence Regularity %, and Scaled Jacobian |

---

### Quick Start

#### 1. Blender Extension (Blender 4.2+ & 5.x LTS)
1. In Blender, navigate to **Edit → Preferences → Get Extensions**.
2. Click the top-right menu icon and choose **Install from Disk...**.
3. Select `instant_meshes_retopo.zip`.
4. Open the 3D Viewport sidebar (**N panel → Retopo**) to remesh any active object in real time.

#### 2. Python 3.13 Extension (`pyretopo`)
```python
import pyretopo
import numpy as np

# V: (N, 3) float32, F: (M, 3) uint32
result = pyretopo.retopologize(
    vertices=V,
    faces=F,
    target_faces=1000,
    intrinsic=True,       # Intrinsic cotangent conditioning
    mirror_symmetry=True  # Bilateral mirror symmetry
)

# Or load directly from an FBX, OBJ, or PLY file:
engine = pyretopo.RetopoEngine()
engine.load_file("character.fbx")
settings = pyretopo.RetopoSettings()
settings.target_face_count = 2500
result = engine.execute(settings)

print(f"Generated {result.faces.shape[0]} quads in {result.elapsed_time_ms:.1f}ms")
print(result.quality_report.summary())
```

#### 3. Command Line Interface (CLI)
```bash
# Remesh an FBX model directly to 2,500 quads:
InstantMeshes2026CLI -f 2500 -i -o output.obj character.fbx

# Remesh an OBJ to exact 1,200 quads with intrinsic cotangent smoothing:
InstantMeshes2026CLI -f 1200 -i -o output.obj input.obj
```

#### 4. Standalone Desktop GUI
Run `Launch_Instant_Meshes_2026.bat` or open `InstantMeshes2026.exe` to use the interactive flow-brush and contour sketching interface. Directly open `.fbx`, `.obj`, or `.ply` models.

---

### Building from Source

Requirements: CMake 3.15+, C++17 compliant compiler (MSVC 2022 / GCC 10+ / Clang 11+).

```bash
git clone https://github.com/MondRubberduck/instant-meshes-2026.git
cd instant-meshes-2026
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

---

### Support & Donations

If you find **Instant Meshes 2026** useful for your 3D workflow or pipeline, you can support ongoing maintenance and development:

* [![Donate with PayPal](https://img.shields.io/badge/Donate-PayPal-00457C?logo=paypal&logoColor=white)](https://www.paypal.com/donate/?hosted_button_id=5XKG8WLLRWZ44) &nbsp; [Donate via PayPal](https://www.paypal.com/donate/?hosted_button_id=5XKG8WLLRWZ44)
* [![Support on Patreon](https://img.shields.io/badge/Support-Patreon-FF424D?logo=patreon&logoColor=white)](https://www.patreon.com/NGallist) &nbsp; [Support on Patreon](https://www.patreon.com/NGallist)

---

### License & Legal Notice

This project is open-source software licensed under the **BSD 3-Clause License**.  
All modifications and modernizations: Copyright (c) 2026 MondRubberduck and Contributors.  
Original Instant Meshes codebase: Copyright (c) 2015 Wenzel Jakob, Daniele Panozzo, Marco Tarini, and Olga Sorkine-Hornung.
