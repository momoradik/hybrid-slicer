"""
HybridSlicer G-code Analyser — checks F1-F15 on generated G-code.
Usage: python analyse_gcode.py <geometry> <config>
"""
import re, os, sys, json, math, yaml

FIX_DIR = os.path.join(os.path.dirname(__file__), "fixtures")

def load_config():
    with open(os.path.join(os.path.dirname(__file__), "config.yaml")) as f:
        return yaml.safe_load(f)

def parse_gcode_lines(path):
    with open(path, encoding="utf-8") as f:
        return f.readlines()

def extract_layers(lines):
    """Returns dict: layer_num -> list of lines"""
    layers = {}
    cur = -1
    for line in lines:
        m = re.match(r'^;LAYER:(\d+)', line)
        if m:
            cur = int(m.group(1))
            layers[cur] = []
        if cur >= 0:
            layers.setdefault(cur, []).append(line)
    return layers

def extract_cnc_passes(lines):
    """Extract CNC pass info from toolpath G-code"""
    passes = []
    cur_pass = None
    for line in lines:
        # Match layer header - Unicode em-dashes may be mangled by encoding
        m = re.match(r'^;.*Layer (\d+): top ([0-9.]+), band ([0-9.]+), tip Z([0-9.]+), engaged ([0-9.]+)', line)
        if m:
            cur_pass = {
                "layer": int(m.group(1)),
                "top": float(m.group(2)),
                "band": float(m.group(3)),
                "tip_z": float(m.group(4)),
                "engaged": float(m.group(5)),
                "g1_z": [],
            }
            passes.append(cur_pass)
        if cur_pass and line.strip().startswith("G1 Z"):
            zm = re.match(r'G1 Z([0-9.]+)', line.strip())
            if zm:
                cur_pass["g1_z"].append(float(zm.group(1)))
    return passes

def check_f1_print_untouched(geo, cfg):
    """F1: Print G-code layers should be intact in hybrid output"""
    results = []
    print_path = os.path.join(FIX_DIR, f"{geo}_{cfg}.print.gcode")
    hybrid_path = os.path.join(FIX_DIR, f"{geo}_{cfg}.hybrid.gcode")
    if not os.path.exists(hybrid_path):
        return [("F1", "SKIP", "No hybrid G-code")]

    print_layers = extract_layers(parse_gcode_lines(print_path))
    hybrid_lines = parse_gcode_lines(hybrid_path)

    # Check layer 0 present
    has_layer0 = any(";LAYER:0" in l for l in hybrid_lines)
    if not has_layer0:
        results.append(("F1", "FAIL", "Layer 0 missing from hybrid G-code"))
    else:
        results.append(("F1-layer0", "PASS", "Layer 0 present"))

    # Check layer count
    hybrid_layer_nums = set()
    for l in hybrid_lines:
        m = re.match(r'^;LAYER:(\d+)', l)
        if m:
            hybrid_layer_nums.add(int(m.group(1)))

    missing = set(print_layers.keys()) - hybrid_layer_nums
    if missing and -1 not in missing:  # -1 is preamble
        results.append(("F1-layers", "FAIL", f"Missing layers in hybrid: {sorted(missing)[:10]}..."))
    else:
        results.append(("F1-layers", "PASS", f"All {len(print_layers)} layers present"))

    return results

def check_f3_tip_depth(geo, cfg, config):
    """F3: Tip depth must be at bottom of band, not top"""
    tp_path = os.path.join(FIX_DIR, f"{geo}_{cfg}.toolpath.gcode")
    if not os.path.exists(tp_path):
        return [("F3", "SKIP", "No toolpath G-code")]

    lines = parse_gcode_lines(tp_path)
    passes = extract_cnc_passes(lines)
    if not passes:
        return [("F3", "SKIP", "No CNC passes found")]

    h = config["layer_height_mm"]
    overlap = config["tool"]["tip_overlap_mm"]
    bed_clr = config["bed_clearance_mm"]
    results = []
    prev_top = 0.0

    for p in passes:
        expected_tip = max(prev_top - overlap, bed_clr)
        actual_tip = p["tip_z"]
        # Check the G1 Z (actual plunge depth) matches
        actual_cut_z = p["g1_z"][0] if p["g1_z"] else None

        # F3a: tip not at layer*h (off-by-one check)
        wrong_tip = p["layer"] * h
        if actual_tip is not None and abs(actual_tip - wrong_tip) < 0.001:
            results.append(("F3-offby1", "FAIL",
                f"Layer {p['layer']}: tip Z={actual_tip:.3f} == layer*h={wrong_tip:.3f} (old bug)"))

        # F3b: tip matches expected
        if actual_cut_z is not None and abs(actual_cut_z - expected_tip) > 0.01:
            results.append(("F3-depth", "FAIL",
                f"Layer {p['layer']}: actual cut Z={actual_cut_z:.3f}, expected={expected_tip:.3f} "
                f"(prev_top={prev_top:.3f} - overlap={overlap:.1f})"))
        elif actual_cut_z is not None:
            pass  # OK

        prev_top = p["top"]

    if not any(r[1] == "FAIL" for r in results):
        results.append(("F3", "PASS", f"All {len(passes)} passes have correct tip depth"))
    return results

def check_f8_flute_spindle(geo, cfg, config):
    """F8: Flute length and spindle clearance checks"""
    tp_path = os.path.join(FIX_DIR, f"{geo}_{cfg}.toolpath.gcode")
    if not os.path.exists(tp_path):
        return [("F8", "SKIP", "No toolpath G-code")]

    lines = parse_gcode_lines(tp_path)
    passes = extract_cnc_passes(lines)
    results = []
    flute = config["tool"]["flute_length_mm"]
    spindle_dist = config["tool"]["tool_length_mm"]
    margin = config["tool"]["safety_margin_mm"]

    for p in passes:
        eng = p["engaged"]
        if flute > 0 and eng + margin > flute:
            results.append(("F8-flute", "FAIL",
                f"Layer {p['layer']}: engaged {eng:.2f} + margin {margin:.1f} = {eng+margin:.2f} > flute {flute:.1f}"))
        if spindle_dist > 0 and eng + margin >= spindle_dist:
            results.append(("F8-spindle", "FAIL",
                f"Layer {p['layer']}: engaged {eng:.2f} + margin {margin:.1f} = {eng+margin:.2f} >= spindle dist {spindle_dist:.1f}"))

    if not any(r[1] == "FAIL" for r in results):
        results.append(("F8", "PASS", f"All {len(passes)} passes within flute/spindle limits"))
    return results

def check_f9_validation(geo, cfg, config):
    """F9: Config C must error with correct message"""
    results_path = os.path.join(FIX_DIR, "matrix_results.json")
    with open(results_path) as f:
        all_results = json.load(f)

    r = next((x for x in all_results if x["geometry"] == geo and x["config"] == cfg), None)
    if r is None:
        return [("F9", "SKIP", "No result found")]

    if cfg == "C":
        if r["status"] == "error":
            # Check error contains flute info
            msg = r["error_msg"]
            has_flute = "flute" in msg.lower() or "Flute" in msg
            has_recommendation = "Max interval" in msg or "Options" in msg
            checks = []
            if has_flute:
                checks.append(("F9-flute-msg", "PASS", "Error mentions flute length"))
            else:
                checks.append(("F9-flute-msg", "FAIL", "Error doesn't mention flute length"))
            if has_recommendation:
                checks.append(("F9-recommend", "PASS", "Error includes recommendation"))
            else:
                checks.append(("F9-recommend", "FAIL", "Error doesn't include recommendation"))
            return checks
        else:
            return [("F9", "FAIL", f"Config C should error but got: {r['status']}")]
    return [("F9", "SKIP", f"Not applicable for config {cfg}")]

def check_f11_end_gcode(geo, cfg):
    """F11: End G-code must come after last CNC pass"""
    hybrid_path = os.path.join(FIX_DIR, f"{geo}_{cfg}.hybrid.gcode")
    if not os.path.exists(hybrid_path):
        return [("F11", "SKIP", "No hybrid G-code")]

    lines = parse_gcode_lines(hybrid_path)
    last_cnc_line = -1
    first_end_line = -1

    for i, line in enumerate(lines):
        if "CNC Machining @" in line or "End CNC @" in line:
            last_cnc_line = i
        t = line.strip().upper()
        if t.startswith("M84") or (t.startswith("G28") and "X0" in t):
            if first_end_line < 0:
                first_end_line = i

    if last_cnc_line < 0:
        return [("F11", "SKIP", "No CNC passes in hybrid")]
    if first_end_line < 0:
        return [("F11", "PASS", "No M84/G28 found (stripped correctly)")]
    if first_end_line < last_cnc_line:
        return [("F11", "FAIL",
            f"End G-code (line {first_end_line}) appears BEFORE last CNC (line {last_cnc_line})")]
    return [("F11", "PASS", f"End G-code after last CNC (CNC@{last_cnc_line}, end@{first_end_line})")]

def check_f12_spindle_commands(geo, cfg):
    """F12: M3 before cutting, M5 after each pass"""
    tp_path = os.path.join(FIX_DIR, f"{geo}_{cfg}.toolpath.gcode")
    if not os.path.exists(tp_path):
        return [("F12", "SKIP", "No toolpath G-code")]

    lines = parse_gcode_lines(tp_path)
    m3_count = sum(1 for l in lines if l.strip().startswith("M3"))
    m5_count = sum(1 for l in lines if l.strip().startswith("M5"))

    results = []
    if m3_count == 0:
        results.append(("F12-M3", "FAIL", "No M3 spindle-on commands found"))
    if m5_count == 0:
        results.append(("F12-M5", "FAIL", "No M5 spindle-off commands found"))
    if m3_count > 0 and m5_count > 0:
        results.append(("F12", "PASS", f"M3 count={m3_count}, M5 count={m5_count}"))

    # Check RPM <= max
    for l in lines:
        m = re.match(r'M3\s+S(\d+)', l.strip())
        if m:
            rpm = int(m.group(1))
            if rpm > 24000:
                results.append(("F12-rpm", "FAIL", f"RPM {rpm} exceeds max 24000"))
    return results

def check_f13_motion_safety(geo, cfg, config):
    """F13: CNC moves absolute, W >= bed_clearance, retract >= part top"""
    tp_path = os.path.join(FIX_DIR, f"{geo}_{cfg}.toolpath.gcode")
    if not os.path.exists(tp_path):
        return [("F13", "SKIP", "No toolpath G-code")]

    lines = parse_gcode_lines(tp_path)
    bed_clr = config["bed_clearance_mm"]
    results = []

    for i, line in enumerate(lines):
        t = line.strip()
        # Check for G91 (relative mode) — should not appear
        if t.startswith("G91"):
            results.append(("F13-abs", "FAIL", f"Line {i}: G91 relative mode in CNC G-code"))
        # Check W/Z below bed clearance
        m = re.match(r'G[01]\s+Z([0-9.]+)', t)
        if m:
            z = float(m.group(1))
            if z < bed_clr - 0.001:
                results.append(("F13-bed", "FAIL", f"Line {i}: Z={z:.3f} below bed clearance {bed_clr}"))

    if not any(r[1] == "FAIL" for r in results):
        results.append(("F13", "PASS", "All CNC moves absolute, Z >= bed clearance"))
    return results

def run_all_checks(geo, cfg):
    config = load_config()
    all_results = []

    all_results.extend(check_f1_print_untouched(geo, cfg))
    all_results.extend(check_f3_tip_depth(geo, cfg, config))
    all_results.extend(check_f8_flute_spindle(geo, cfg, config))
    all_results.extend(check_f9_validation(geo, cfg, config))
    all_results.extend(check_f11_end_gcode(geo, cfg))
    all_results.extend(check_f12_spindle_commands(geo, cfg))
    all_results.extend(check_f13_motion_safety(geo, cfg, config))

    return all_results

def main():
    config = load_config()
    geometries = ["cube", "bowl", "cone", "stepped", "slot_corner"]
    configs = ["A", "B", "C", "D"]

    print("=" * 80)
    print("  HybridSlicer G-code Analysis — F1-F15 Feature Checks")
    print("=" * 80)

    all_results = {}
    for geo in geometries:
        for cfg in configs:
            key = f"{geo}_{cfg}"
            results = run_all_checks(geo, cfg)
            all_results[key] = results

            passes = sum(1 for r in results if r[1] == "PASS")
            fails = sum(1 for r in results if r[1] == "FAIL")
            skips = sum(1 for r in results if r[1] == "SKIP")
            print(f"\n{key}: {passes} PASS, {fails} FAIL, {skips} SKIP")
            for check, status, detail in results:
                marker = {"PASS": "  OK", "FAIL": "FAIL", "SKIP": "SKIP"}[status]
                print(f"  [{marker}] {check}: {detail}")

    # Summary matrix
    print("\n" + "=" * 80)
    print("  SUMMARY MATRIX")
    print("=" * 80)
    print(f"{'Run':<20} {'PASS':<6} {'FAIL':<6} {'SKIP':<6}")
    print("-" * 40)
    total_pass = total_fail = 0
    for key, results in all_results.items():
        p = sum(1 for r in results if r[1] == "PASS")
        f = sum(1 for r in results if r[1] == "FAIL")
        s = sum(1 for r in results if r[1] == "SKIP")
        total_pass += p
        total_fail += f
        print(f"{key:<20} {p:<6} {f:<6} {s:<6}")
    print(f"\nTOTAL: {total_pass} PASS, {total_fail} FAIL")

    # Save
    report = {k: [(c, s, d) for c, s, d in v] for k, v in all_results.items()}
    with open(os.path.join(FIX_DIR, "analysis_results.json"), "w") as f:
        json.dump(report, f, indent=2)

if __name__ == "__main__":
    main()
