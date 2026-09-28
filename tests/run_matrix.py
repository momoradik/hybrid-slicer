"""
HybridSlicer CNC Feature Test Matrix Runner
Slices each geometry, runs hybrid pipeline for configs A-D, saves G-code to fixtures/.
"""
import requests, json, os, sys, time

BASE = "http://localhost:8080/api"
GEO_DIR = os.path.join(os.path.dirname(__file__), "geometry")
FIX_DIR = os.path.join(os.path.dirname(__file__), "fixtures")
os.makedirs(FIX_DIR, exist_ok=True)

def api(method, path, **kw):
    r = getattr(requests, method)(f"{BASE}{path}", **kw)
    if r.status_code >= 400:
        print(f"  ERROR {r.status_code}: {r.text[:300]}")
    return r

def get_ids():
    machines = api("get", "/machine-profiles").json()
    profiles = api("get", "/print-profiles").json()
    materials = api("get", "/materials").json()
    return machines[0]["id"], profiles[0]["id"], materials[0]["id"]

def create_tool():
    """Create the Holex E0307 test tool matching config.yaml"""
    r = api("post", "/tools", json={
        "name": "TEST_E0307", "type": "BallEndMill",
        "diameterMm": 3.0, "fluteLengthMm": 7.0, "shankDiameterMm": 3.0,
        "toolLengthMm": 19.0, "fluteCount": 2, "toolMaterial": "HSS",
        "tipOverlapMm": 1.0, "tipShape": "rounded",
        "spindleRadiusMm": 10.0, "safetyMarginMm": 0.5,
        "recommendedRpm": 10000, "recommendedFeedMmPerMin": 500,
        "maxDepthOfCutMm": 2.0
    })
    return r.json()["id"]

def upload_and_slice(stl_path, name, machine_id, profile_id, material_id):
    with open(stl_path, "rb") as f:
        r = api("post", "/jobs/upload-stl", files={"file": (os.path.basename(stl_path), f)},
                data={"jobName": name, "machineProfileId": machine_id,
                      "printProfileId": profile_id, "materialId": material_id,
                      "supportEnabled": "false", "gcodeHoming": "false",
                      "applyCustomGCodeBlocks": "false"})
    job_id = r.json()["jobId"]
    r2 = api("post", f"/jobs/{job_id}/slice")
    layers = r2.json().get("totalLayers", 0)
    # Save print gcode
    gc = api("get", f"/jobs/{job_id}/print-gcode").text
    print_path = os.path.join(FIX_DIR, f"{name}.print.gcode")
    with open(print_path, "w", encoding="utf-8") as f:
        f.write(gc)
    return job_id, layers, print_path

def run_config(job_id, tool_id, cfg_name, cfg, geo_name):
    """Run generate-toolpaths (and optionally plan-hybrid) for one config."""
    prefix = f"{geo_name}_{cfg_name}"
    payload = {"cncToolId": tool_id, "machineEveryNLayers": cfg["N"]}
    if cfg.get("auto"):
        payload["autoMachiningFrequency"] = True

    r = api("post", f"/jobs/{job_id}/generate-toolpaths", json=payload)
    result = r.json()

    # Save toolpath gcode
    tp = api("get", f"/jobs/{job_id}/toolpath-gcode")
    if tp.status_code == 200:
        tp_path = os.path.join(FIX_DIR, f"{prefix}.toolpath.gcode")
        with open(tp_path, "w", encoding="utf-8") as f:
            f.write(tp.text)

    # Try plan-hybrid
    r2 = api("post", f"/jobs/{job_id}/plan-hybrid", json={"machineEveryNLayers": cfg["N"]})
    if r2.status_code < 400:
        hg = api("get", f"/jobs/{job_id}/gcode")
        if hg.status_code == 200:
            hg_path = os.path.join(FIX_DIR, f"{prefix}.hybrid.gcode")
            with open(hg_path, "w", encoding="utf-8") as f:
                f.write(hg.text)

    return {
        "config": cfg_name,
        "geometry": geo_name,
        "status": "error" if r.status_code >= 400 else "ok",
        "http_code": r.status_code,
        "toolpath_count": result.get("toolpathCount", 0),
        "warnings": result.get("warnings", []),
        "error_msg": result.get("message", "") if r.status_code >= 400 else "",
        "unmachinable": len(result.get("unmachinableRegions", []))
    }

CONFIGS = {
    "A": {"N": 5},
    "B": {"N": 10},
    "C": {"N": 40},  # should fail: band 8.0 + overlap 1.0 + margin 0.5 = 9.5 > flute 7.0
    "D": {"N": 10, "auto": True},
}

GEOMETRIES = ["cube", "bowl", "cone", "stepped", "slot_corner"]

def main():
    print("=== HybridSlicer CNC Feature Test Matrix ===\n")
    machine_id, profile_id, material_id = get_ids()
    tool_id = create_tool()
    print(f"Tool: {tool_id}\n")

    results = []
    for geo in GEOMETRIES:
        stl_path = os.path.join(GEO_DIR, f"{geo}.stl")
        if not os.path.exists(stl_path):
            print(f"SKIP {geo}: STL not found")
            continue

        print(f"\n--- {geo} ---")
        for cfg_name, cfg in CONFIGS.items():
            name = f"{geo}_{cfg_name}"
            print(f"  Config {cfg_name} (N={cfg['N']}, auto={cfg.get('auto', False)})...", end=" ")
            try:
                job_id, layers, print_path = upload_and_slice(stl_path, name, machine_id, profile_id, material_id)
                r = run_config(job_id, tool_id, cfg_name, cfg, geo)
                results.append(r)
                status = r["status"]
                if status == "error":
                    print(f"ERROR (expected for C): {r['error_msg'][:100]}")
                else:
                    print(f"OK ({r['toolpath_count']} passes, {r['unmachinable']} unmachinable, {len(r['warnings'])} warnings)")
            except Exception as e:
                print(f"EXCEPTION: {e}")
                results.append({"config": cfg_name, "geometry": geo, "status": "exception", "error_msg": str(e)})

    # Summary
    print("\n\n=== MATRIX SUMMARY ===")
    print(f"{'Geometry':<15} {'Cfg':<5} {'Status':<10} {'Passes':<8} {'Unmach':<8} {'Warnings':<8}")
    print("-" * 60)
    for r in results:
        print(f"{r['geometry']:<15} {r['config']:<5} {r['status']:<10} {r.get('toolpath_count',''):<8} {r.get('unmachinable',''):<8} {len(r.get('warnings',[])):<8}")

    # Save results
    with open(os.path.join(FIX_DIR, "matrix_results.json"), "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nResults saved to {FIX_DIR}/matrix_results.json")

if __name__ == "__main__":
    main()
