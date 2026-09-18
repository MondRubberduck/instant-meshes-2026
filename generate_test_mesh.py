import math

def generate_torus(R=1.0, r=0.4, num_R=48, num_r=24, filename="torus.obj"):
    vertices = []
    faces = []
    
    for i in range(num_R):
        u = (i / num_R) * 2 * math.pi
        for j in range(num_r):
            v = (j / num_r) * 2 * math.pi
            x = (R + r * math.cos(v)) * math.cos(u)
            y = (R + r * math.cos(v)) * math.sin(u)
            z = r * math.sin(v)
            vertices.append((x, y, z))
            
    for i in range(num_R):
        next_i = (i + 1) % num_R
        for j in range(num_r):
            next_j = (j + 1) % num_r
            
            p0 = i * num_r + j + 1
            p1 = next_i * num_r + j + 1
            p2 = next_i * num_r + next_j + 1
            p3 = i * num_r + next_j + 1
            
            # Triangulate
            faces.append((p0, p1, p2))
            faces.append((p0, p2, p3))
            
    with open(filename, "w") as f:
        for v in vertices:
            f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
        for face in faces:
            f.write(f"f {face[0]} {face[1]} {face[2]}\n")
            
    print(f"Generated {filename} with {len(vertices)} vertices and {len(faces)} triangles.")

def generate_contour(R=1.0, r=0.4, filename="contour.obj"):
    # Generate a circular guide loop around the outer rim of the torus
    pts = []
    steps = 32
    for i in range(steps):
        u = (i / steps) * 2 * math.pi
        x = (R + r) * math.cos(u)
        y = (R + r) * math.sin(u)
        z = 0.0
        pts.append((x, y, z))
        
    with open(filename, "w") as f:
        for p in pts:
            f.write(f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}\n")
        indices = list(range(1, steps + 1))
        indices.append(1) # close loop
        f.write("l " + " ".join(map(str, indices)) + "\n")
        
    print(f"Generated {filename} with {len(pts)} points closed loop.")

if __name__ == "__main__":
    generate_torus()
    generate_contour()
