"""模块名: CompareV2Blender
功能概述: 用 Blender 真实 OBJ 导入与有限修改器核对二期原生作品的几何。
对外接口: blender --background --factory-startup --python 此文件 -- artifacts output.json。
依赖关系: Blender bpy；不安装插件，不读取用户工程或保存用户配置。
输入输出: 三个验收作品和源/求值 OBJ 到版本、SHA256、数量和误差报告。
异常与错误: 缺件、已有输出、导入或数值不一致抛错，并保留失败报告。
维护说明: 保持坐标数值不作 Y/Z 换轴；仅比较几何，不证明 UV/法线或完整 Blender 等价。
"""

import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector
from mathutils.kdtree import KDTree

TOLERANCE = 1e-5


def read_obj(path):
    """读取本项目输出的正索引位置与面环，作为导入前独立基准。"""
    positions, faces = [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            positions.append(tuple(float(value) for value in fields[1:4]))
        elif fields[0] == "f":
            faces.append(tuple(int(value.split("/")[0]) - 1 for value in fields[1:]))
    assert positions and faces, f"Empty OBJ: {path}"
    assert all(math.isfinite(value) for point in positions for value in point)
    assert all(0 <= index < len(positions) for face in faces for index in face)
    return positions, faces


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_ring(indices):
    # 只允许环起点不同，不反转绕序。
    return min(indices[offset:] + indices[:offset] for offset in range(len(indices)))


def compare_geometry(mesh, expected):
    """以位置匹配映射独立顶点序号，再检查完整多边形环与绕序。"""
    positions, faces = expected
    actual_positions = [tuple(vertex.co) for vertex in mesh.vertices]
    assert len(actual_positions) == len(positions), "Vertex count differs"
    assert len(mesh.polygons) == len(faces), "Polygon count differs"
    tree = KDTree(len(positions))
    for index, position in enumerate(positions):
        tree.insert(Vector(position), index)
    tree.balance()
    mapping, errors = [], []
    for position in actual_positions:
        _, index, distance = tree.find(Vector(position))
        assert distance <= TOLERANCE, f"Position error {distance} > {TOLERANCE}"
        mapping.append(index)
        errors.append(distance)
    assert len(set(mapping)) == len(positions), "Position mapping is not one-to-one"
    expected_rings = sorted(canonical_ring(face) for face in faces)
    actual_rings = sorted(
        canonical_ring(tuple(mapping[index] for index in face.vertices))
        for face in mesh.polygons
    )
    assert actual_rings == expected_rings, "Polygon connectivity or winding differs"
    return {"vertices": len(positions), "faces": len(faces),
            "maximumPositionError": max(errors), "connectivityAndWinding": "passed"}


def import_obj(path):
    """明确指定不换轴，不用 Blender 默认的 OBJ Y-up 转换混淆数值对照。"""
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.obj_import(filepath=str(path), forward_axis="Y", up_axis="Z")
    objects = [obj for obj in bpy.context.selected_objects if obj.type == "MESH"]
    assert len(objects) == 1, f"Expected one imported object: {path}"
    return objects[0]


def compare_import(path):
    obj = import_obj(path)
    result = compare_geometry(obj.data, read_obj(path))
    result.update({"file": path.name, "sha256": sha256(path), "status": "passed"})
    return result


def compare_modifiers(artifacts, name):
    """从真实场景读取选项；仅处理验收作品的单位变换单对象固定链。"""
    scene_path = artifacts / f"{name}.m3dscene"
    document = json.loads(scene_path.read_text(encoding="utf-8"))
    assert document["version"] == 3
    assert len(document["editableMeshes"]) == 1
    mesh = document["editableMeshes"][0]
    entities = [entity for entity in document["entities"]
                if entity.get("editableMesh") == mesh["id"]]
    assert len(entities) == 1
    entity = entities[0]
    assert entity["parent"] == 0
    assert entity["transform"] == {"position": [0, 0, 0], "rotation": [1, 0, 0, 0],
                                    "scale": [1, 1, 1]}, "Requires identity local/world transform"
    obj = import_obj(artifacts / f"{name}-source.obj")
    configured = []
    for options in mesh.get("modifiers", []):
        if not options["enabled"]:
            continue
        if options["type"] == "Mirror":
            modifier = obj.modifiers.new("Mini3D reference Mirror", "MIRROR")
            modifier.use_axis = tuple(axis == options["axis"] for axis in ("X", "Y", "Z"))
            modifier.use_mirror_merge = options["merge"]
            modifier.merge_threshold = options["threshold"]
            modifier.use_clip = options["clipping"]
        elif options["type"] == "Subdivision":
            modifier = obj.modifiers.new("Mini3D reference Catmull-Clark", "SUBSURF")
            modifier.subdivision_type = "CATMULL_CLARK"
            modifier.levels = options["levels"]
            modifier.render_levels = options["levels"]
            # 本项目是有限级离散位置，不承诺 Blender 的极限曲面放置。
            modifier.use_limit_surface = False
            modifier.boundary_smooth = "ALL"
        else:
            raise AssertionError(f"Unsupported modifier: {options['type']}")
        configured.append(options)
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    evaluated_mesh = evaluated.to_mesh()
    try:
        result = compare_geometry(evaluated_mesh,
                                  read_obj(artifacts / f"{name}-evaluated.obj"))
    finally:
        evaluated.to_mesh_clear()
    result.update({"scene": scene_path.name, "sceneSHA256": sha256(scene_path),
                   "modifiers": configured, "status": "passed"})
    return result


def main():
    arguments = sys.argv[sys.argv.index("--") + 1:]
    assert len(arguments) == 2, "Expected artifacts-directory output.json"
    artifacts, output = (Path(value).resolve() for value in arguments)
    assert artifacts.is_dir() and not output.exists(), "Missing input or output already exists"
    report = {"caseId": "v2-external-blender", "blenderVersion": bpy.app.version_string,
              "blenderBuildHash": bpy.app.build_hash.decode("ascii"),
              "fixedReferenceVersion": "4.5.0", "tolerance": TOLERANCE,
              "coordinates": "OBJ import forward Y/up Z: numeric coordinates preserved; "
                             "Mini3D remains Y-up, Blender remains Z-up",
              "subdivision": "Finite Catmull-Clark; use_limit_surface=False; boundary_smooth=ALL",
              "imports": [], "modifierComparisons": [],
              "limitations": ["Installed Blender version may differ from the fixed reference.",
                              "This checks positions, face connectivity and winding, not UV, "
                              "split normals, materials, clipping input or UI parity.",
                              "Only identity-transform native acceptance works are compared."],
              "status": "failed"}
    try:
        for name in ("shell", "symmetric", "subdivision"):
            report["imports"].append(compare_import(artifacts / f"{name}-source.obj"))
            if name != "shell":
                report["imports"].append(compare_import(artifacts / f"{name}-evaluated.obj"))
                report["modifierComparisons"].append(compare_modifiers(artifacts, name))
        report["status"] = "passed-with-limitations"
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8", newline="\n") as handle:
            json.dump(report, handle, ensure_ascii=False, indent=2)
            handle.write("\n")


if __name__ == "__main__":
    main()
