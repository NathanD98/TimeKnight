"""
TimeKnight paladin kit - one-shot editor setup.

Run it inside the Unreal Editor:   Tools > Execute Python Script...  (pick this file)
 (needs Edit > Plugins > "Python Editor Script Plugin" enabled; restart once if you had to enable it)

What it does
  1. Imports PaladinKit/Meshes/*.glb into /Game/Paladin/Meshes as static meshes
  2. Creates /Game/Paladin/Materials/M_PaladinFlat (two-sided, Color / Metallic / Roughness parameters)
  3. Prints the material slot names of every imported mesh so you can check them

If step 1 does not produce the expected assets on your engine version, just drag the .glb files from
PaladinKit/Meshes into the Content Browser folder /Game/Paladin/Meshes yourself and keep the asset names.
"""
import os
import unreal

MESH_NAMES = [
    "SM_Paladin_Sword", "SM_Paladin_Shield", "SM_Paladin_Hair",
    "SM_Paladin_Pauldron", "SM_Paladin_Chest", "SM_Paladin_Tabard",
]
MESH_DIR = "/Game/Paladin/Meshes"
MAT_DIR = "/Game/Paladin/Materials"
MAT_NAME = "M_PaladinFlat"

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def import_meshes():
    src_dir = os.path.join(unreal.Paths.project_dir(), "PaladinKit", "Meshes")
    tasks = []
    for name in MESH_NAMES:
        path = os.path.join(src_dir, name + ".glb")
        if not os.path.exists(path):
            unreal.log_warning("[Paladin] missing source file: " + path)
            continue
        if eal.does_asset_exist("%s/%s" % (MESH_DIR, name)):
            unreal.log("[Paladin] %s already imported, skipping" % name)
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", path)
        task.set_editor_property("destination_path", MESH_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    if tasks:
        tools.import_asset_tasks(tasks)

    for name in MESH_NAMES:
        asset_path = "%s/%s" % (MESH_DIR, name)
        if not eal.does_asset_exist(asset_path):
            unreal.log_warning("[Paladin] %s was NOT created. Drag PaladinKit/Meshes/%s.glb into %s manually and "
                               "make sure the asset ends up named %s." % (name, name, MESH_DIR, name))
            continue
        mesh = eal.load_asset(asset_path)
        try:
            slots = [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("static_materials")]
        except Exception:
            slots = ["<could not read>"]
        unreal.log("[Paladin] %-22s material slots: %s" % (name, slots))


def make_material():
    path = "%s/%s" % (MAT_DIR, MAT_NAME)
    if eal.does_asset_exist(path):
        unreal.log("[Paladin] %s already exists, skipping" % MAT_NAME)
        return
    lib = unreal.MaterialEditingLibrary
    mat = tools.create_asset(MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", True)

    color = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, -200)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))

    metal = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 0)
    metal.set_editor_property("parameter_name", "Metallic")
    metal.set_editor_property("default_value", 0.0)

    rough = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 150)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.5)

    lib.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log("[Paladin] created " + path)


make_material()
import_meshes()
unreal.log("[Paladin] setup finished - compile the C++ project and press Play.")
