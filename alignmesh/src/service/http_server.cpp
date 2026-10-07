// alignmesh HTTP service — serves the core inspection API on a local IP endpoint.
// Single-threaded, deterministic. The official compute path.

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "alignmesh/service/result_package.h"
#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/io/step_io.h"
#include "alignmesh/config.h"

#if ALIGNMESH_WITH_STEP
#include <STEPControl_Reader.hxx>
#include <Interface_Static.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Pnt.hxx>
#endif

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "dbghelp.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dbghelp.h>
using socket_t = SOCKET;
#define CLOSE_SOCKET closesocket

// Crash handler: capture stack trace on access violation.
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    std::cerr << "\n=== CRASH: exception 0x" << std::hex
              << ep->ExceptionRecord->ExceptionCode << " at 0x"
              << ep->ExceptionRecord->ExceptionAddress << std::dec << " ===\n";

    // Walk the stack.
    HANDLE proc = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymInitialize(proc, NULL, TRUE);

    CONTEXT* ctx = ep->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset    = ctx->Rip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;
    frame.AddrStack.Mode   = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrFrame.Mode   = AddrModeFlat;

    char sym_buf[sizeof(SYMBOL_INFO) + 256];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    std::cerr << "Stack trace:\n";
    for (int i = 0; i < 40; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame,
                         ctx, NULL, SymFunctionTableAccess64,
                         SymGetModuleBase64, NULL))
            break;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, frame.AddrPC.Offset, &disp, sym)) {
            std::cerr << "  [" << i << "] " << sym->Name << " +0x"
                      << std::hex << disp << std::dec << "\n";
        } else {
            std::cerr << "  [" << i << "] 0x" << std::hex
                      << frame.AddrPC.Offset << std::dec << "\n";
        }
    }
    std::cerr << "=== END CRASH ===" << std::endl;
    return EXCEPTION_CONTINUE_SEARCH;  // let the OS terminate
}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using socket_t = int;
#define CLOSE_SOCKET close
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct WinsockInit {
#ifdef _WIN32
    WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2,2), &d); }
    ~WinsockInit() { WSACleanup(); }
#endif
};

// Read the full HTTP request from a socket.
// Binary-safe. Handles large uploads (tens of MB) efficiently by parsing
// headers once, then bulk-reading the body.
std::string read_request(socket_t client) {
    std::string req;
    char buf[65536];  // 64KB buffer for fast large-file reads
    bool headers_done = false;
    std::size_t body_start = 0;
    std::size_t content_len = 0;

    while (true) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, static_cast<std::size_t>(n));

        if (!headers_done) {
            auto hdr_end = req.find("\r\n\r\n");
            if (hdr_end == std::string::npos) {
                // Still reading headers. If it's a GET, we might be done.
                if (req.size() >= 4 && req.compare(0, 4, "GET ") == 0)
                    break;
                continue;
            }
            // Headers complete.
            headers_done = true;
            body_start = hdr_end + 4;
            // Case-insensitive search for Content-Length header.
            std::string headers_lower = req.substr(0, hdr_end);
            for (auto& ch : headers_lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            auto cl_pos = headers_lower.find("content-length:");
            if (cl_pos == std::string::npos) break;  // no body (or chunked — read until close)
            auto val_start = req.find_first_of("0123456789", cl_pos);
            auto val_end = req.find("\r\n", val_start);
            content_len = static_cast<std::size_t>(
                std::stoll(req.substr(val_start, val_end - val_start)));
            // Pre-allocate to avoid repeated reallocation.
            req.reserve(body_start + content_len + 1);
        }

        // Check if we have the full body.
        if (req.size() - body_start >= content_len) break;
    }
    return req;
}

// Send an HTTP response.
void send_response(socket_t client, int status, const std::string& content_type,
                   const std::string& body) {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << " OK\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
         << "Access-Control-Allow-Headers: Content-Type\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << body;
    std::string r = resp.str();
    send(client, r.c_str(), static_cast<int>(r.size()), 0);
}

// Serve a static file from the wwwroot directory.
bool serve_static(socket_t client, const std::string& url_path, const std::string& wwwroot) {
    // Map URL path to file path. "/" -> "/index.html"
    std::string file_path = url_path;
    if (file_path == "/") file_path = "/index.html";

    // Security: reject path traversal
    if (file_path.find("..") != std::string::npos) return false;

    std::string resolved_str = wwwroot + "\\" + file_path.substr(1);
    // Normalize all slashes to backslash for Windows.
    for (auto& c : resolved_str) if (c == '/') c = '\\';
    std::ifstream ifs(resolved_str, std::ios::binary | std::ios::ate);
    if (!ifs) return false;

    auto size = ifs.tellg();
    ifs.seekg(0);
    std::string body(static_cast<std::size_t>(size), '\0');
    ifs.read(body.data(), size);

    // Content type from extension
    std::string ext;
    auto dot = resolved_str.rfind('.');
    if (dot != std::string::npos) ext = resolved_str.substr(dot);
    std::string ct = "application/octet-stream";
    if (ext == ".html") ct = "text/html; charset=utf-8";
    else if (ext == ".js") ct = "application/javascript";
    else if (ext == ".css") ct = "text/css";
    else if (ext == ".png") ct = "image/png";
    else if (ext == ".svg") ct = "image/svg+xml";
    else if (ext == ".json") ct = "application/json";
    else if (ext == ".ico") ct = "image/x-icon";

    send_response(client, 200, ct, body);
    return true;
}

// Extract the request path from the first line.
std::string extract_path(const std::string& req) {
    auto sp1 = req.find(' ');
    auto sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return "/";
    return req.substr(sp1 + 1, sp2 - sp1 - 1);
}

// Extract the body from a request.
std::string extract_body(const std::string& req) {
    auto pos = req.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return req.substr(pos + 4);
}

// Simple JSON value extractor (no full parser needed).
std::string json_string(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    auto q1 = json.find('"', pos + 1);
    if (q1 == std::string::npos) return "";
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

double json_number(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    auto start = json.find_first_of("-0123456789.", pos + 1);
    if (start == std::string::npos) return 0;
    return std::stod(json.substr(start));
}

// Parse a JSON array of landmark pair objects.
// Each element: {"ref_x":..., "ref_y":..., "ref_z":..., "meas_x":..., "meas_y":..., "meas_z":..., "weight":...}
std::vector<alignmesh::service::LandmarkPair> parse_landmarks(const std::string& json) {
    std::vector<alignmesh::service::LandmarkPair> result;
    auto arr_pos = json.find("\"landmarks\"");
    if (arr_pos == std::string::npos) return result;
    auto bracket = json.find('[', arr_pos);
    if (bracket == std::string::npos) return result;
    auto end_bracket = json.find(']', bracket);
    if (end_bracket == std::string::npos) return result;

    std::string arr = json.substr(bracket, end_bracket - bracket + 1);
    // Find each object {...} in the array
    std::size_t pos = 0;
    while (true) {
        auto obj_start = arr.find('{', pos);
        if (obj_start == std::string::npos) break;
        auto obj_end = arr.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        std::string obj = arr.substr(obj_start, obj_end - obj_start + 1);

        alignmesh::service::LandmarkPair lp;
        lp.ref_x = json_number(obj, "ref_x");
        lp.ref_y = json_number(obj, "ref_y");
        lp.ref_z = json_number(obj, "ref_z");
        lp.meas_x = json_number(obj, "meas_x");
        lp.meas_y = json_number(obj, "meas_y");
        lp.meas_z = json_number(obj, "meas_z");
        lp.weight = json_number(obj, "weight");
        if (lp.weight <= 0) lp.weight = 1.0;
        result.push_back(lp);

        pos = obj_end + 1;
    }
    return result;
}

std::vector<alignmesh::service::RPSPointSpec> parse_rps_points(const std::string& json) {
    std::vector<alignmesh::service::RPSPointSpec> result;
    auto arr_pos = json.find("\"rps_points\"");
    if (arr_pos == std::string::npos) return result;
    auto bracket = json.find('[', arr_pos);
    if (bracket == std::string::npos) return result;

    // Find matching close bracket (may contain nested arrays for locks).
    int depth = 0;
    std::size_t end_bracket = bracket;
    for (std::size_t i = bracket; i < json.size(); ++i) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') { depth--; if (depth == 0) { end_bracket = i; break; } }
    }

    std::string arr = json.substr(bracket, end_bracket - bracket + 1);

    // Parse each top-level object in the array.
    // Objects may contain nested "locks" arrays, so we track brace depth.
    std::size_t pos = 0;
    while (pos < arr.size()) {
        auto obj_start = arr.find('{', pos);
        if (obj_start == std::string::npos) break;

        // Find matching close brace.
        int brace_depth = 0;
        std::size_t obj_end = obj_start;
        for (std::size_t i = obj_start; i < arr.size(); ++i) {
            if (arr[i] == '{') brace_depth++;
            else if (arr[i] == '}') { brace_depth--; if (brace_depth == 0) { obj_end = i; break; } }
        }
        std::string obj = arr.substr(obj_start, obj_end - obj_start + 1);

        alignmesh::service::RPSPointSpec rp;
        rp.x = json_number(obj, "x");
        rp.y = json_number(obj, "y");
        rp.z = json_number(obj, "z");

        // Parse nested "locks" array.
        auto locks_pos = obj.find("\"locks\"");
        if (locks_pos != std::string::npos) {
            auto lb = obj.find('[', locks_pos);
            auto le = obj.find(']', lb);
            if (lb != std::string::npos && le != std::string::npos) {
                std::string locks_arr = obj.substr(lb, le - lb + 1);
                std::size_t lpos = 0;
                while (true) {
                    auto ls = locks_arr.find('{', lpos);
                    if (ls == std::string::npos) break;
                    auto lend = locks_arr.find('}', ls);
                    if (lend == std::string::npos) break;
                    std::string lock_obj = locks_arr.substr(ls, lend - ls + 1);

                    alignmesh::service::RPSLockSpec lock;
                    lock.axis = json_string(lock_obj, "axis");
                    if (lock.axis.empty()) lock.axis = "normal";
                    lock.weight = json_number(lock_obj, "weight");
                    if (lock.weight <= 0) lock.weight = 1.0;
                    rp.locks.push_back(lock);

                    lpos = lend + 1;
                }
            }
        }

        // Backward compat: if no locks array, try legacy "priority" field.
        if (rp.locks.empty()) {
            std::string axis = json_string(obj, "axis");
            if (axis.empty()) axis = "normal";
            double w = json_number(obj, "weight");
            if (w <= 0) w = 1.0;
            rp.locks.push_back({axis, w});
        }

        result.push_back(rp);
        pos = obj_end + 1;
    }
    return result;
}

std::string coarse_step_preview(const std::string& file_path) {
#if ALIGNMESH_WITH_STEP
    Interface_Static::SetRVal("xstep.cascade.unit", 1.0);
    STEPControl_Reader reader;
    if (reader.ReadFile(file_path.c_str()) != IFSelect_RetDone) return {};
    if (reader.TransferRoots() == 0) return {};
    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) return {};

    // Coarse tessellation: 1% of bounding diagonal (vs 0.05% for analysis).
    Bnd_Box bbox;
    BRepBndLib::Add(shape, bbox);
    double xmin,ymin,zmin,xmax,ymax,zmax;
    bbox.Get(xmin,ymin,zmin,xmax,ymax,zmax);
    double diag = std::sqrt((xmax-xmin)*(xmax-xmin)+(ymax-ymin)*(ymax-ymin)+(zmax-zmin)*(zmax-zmin));
    double deflection = std::max(0.05, diag * 0.01);  // 20x coarser than analysis

    BRepMesh_IncrementalMesh mesher(shape, deflection, false, 0.5, true);
    mesher.Perform();

    // Extract vertices + triangles into binary STL.
    std::vector<float> tri_data; // 12 floats per triangle (normal + 3 verts)
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) poly = BRep_Tool::Triangulation(face, loc);
        if (poly.IsNull()) continue;
        gp_Trsf trsf = loc.Transformation();
        bool rev = (face.Orientation() == TopAbs_REVERSED);
        for (int i = 1; i <= poly->NbTriangles(); ++i) {
            int n1,n2,n3;
            poly->Triangle(i).Get(n1,n2,n3);
            if (rev) std::swap(n2,n3);
            gp_Pnt p0 = poly->Node(n1).Transformed(trsf);
            gp_Pnt p1 = poly->Node(n2).Transformed(trsf);
            gp_Pnt p2 = poly->Node(n3).Transformed(trsf);
            // Face normal
            double ax=p1.X()-p0.X(), ay=p1.Y()-p0.Y(), az=p1.Z()-p0.Z();
            double bx=p2.X()-p0.X(), by=p2.Y()-p0.Y(), bz=p2.Z()-p0.Z();
            double nx=ay*bz-az*by, ny=az*bx-ax*bz, nz=ax*by-ay*bx;
            double len=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(len>0){nx/=len;ny/=len;nz/=len;}
            tri_data.push_back(static_cast<float>(nx));
            tri_data.push_back(static_cast<float>(ny));
            tri_data.push_back(static_cast<float>(nz));
            tri_data.push_back(static_cast<float>(p0.X()));
            tri_data.push_back(static_cast<float>(p0.Y()));
            tri_data.push_back(static_cast<float>(p0.Z()));
            tri_data.push_back(static_cast<float>(p1.X()));
            tri_data.push_back(static_cast<float>(p1.Y()));
            tri_data.push_back(static_cast<float>(p1.Z()));
            tri_data.push_back(static_cast<float>(p2.X()));
            tri_data.push_back(static_cast<float>(p2.Y()));
            tri_data.push_back(static_cast<float>(p2.Z()));
        }
    }
    uint32_t ntri = static_cast<uint32_t>(tri_data.size() / 12);
    if (ntri == 0) return {};
    std::size_t stl_size = 80 + 4 + static_cast<std::size_t>(ntri) * 50;
    std::string buf(stl_size, '\0');
    const char hdr[] = "alignmesh coarse preview";
    std::memcpy(buf.data(), hdr, sizeof(hdr)-1);
    std::memcpy(buf.data()+80, &ntri, 4);
    char* ptr = buf.data()+84;
    for (uint32_t i = 0; i < ntri; ++i) {
        std::memcpy(ptr, &tri_data[i*12], 48); ptr += 48;
        uint16_t attr = 0; std::memcpy(ptr, &attr, 2); ptr += 2;
    }
    return buf;
#else
    (void)file_path;
    return {};
#endif
}

// Serialize a TriangleMesh to binary STL bytes in memory.
// Display-only (float32 precision loss is acceptable for preview).
std::string mesh_to_binary_stl(const alignmesh::geometry::TriangleMesh& mesh) {
    auto nf = mesh.num_triangles();
    auto nv = mesh.num_vertices();
    if (nf == 0 || nv == 0) return {};

    uint32_t tri_count = static_cast<uint32_t>(nf);
    std::size_t size = 80 + 4 + static_cast<std::size_t>(tri_count) * 50;
    std::string buf(size, '\0');

    // 80-byte header
    const char hdr[] = "alignmesh preview tessellation";
    std::memcpy(buf.data(), hdr, sizeof(hdr) - 1);
    // Triangle count
    std::memcpy(buf.data() + 80, &tri_count, 4);

    const auto& V = mesh.vertices();
    const auto& F = mesh.triangles();
    char* ptr = buf.data() + 84;

    for (Eigen::Index i = 0; i < nf; ++i) {
        Eigen::Vector3d v0 = V.col(F(0, i));
        Eigen::Vector3d v1 = V.col(F(1, i));
        Eigen::Vector3d v2 = V.col(F(2, i));
        Eigen::Vector3d n = (v1 - v0).cross(v2 - v0);
        double len = n.norm();
        if (len > 0) n /= len;

        float fn[3] = {static_cast<float>(n.x()), static_cast<float>(n.y()), static_cast<float>(n.z())};
        float fv[9] = {
            static_cast<float>(v0.x()), static_cast<float>(v0.y()), static_cast<float>(v0.z()),
            static_cast<float>(v1.x()), static_cast<float>(v1.y()), static_cast<float>(v1.z()),
            static_cast<float>(v2.x()), static_cast<float>(v2.y()), static_cast<float>(v2.z()),
        };
        std::memcpy(ptr, fn, 12); ptr += 12;
        std::memcpy(ptr, fv, 36); ptr += 36;
        uint16_t attr = 0;
        std::memcpy(ptr, &attr, 2); ptr += 2;
    }
    return buf;
}

// Convert ResultPackage to JSON response.
std::string result_to_json(const alignmesh::service::ResultPackage& pkg) {
    auto verdict_str = [](alignmesh::analysis::Verdict v) -> const char* {
        switch (v) {
        case alignmesh::analysis::Verdict::PASS:    return "PASS";
        case alignmesh::analysis::Verdict::WARNING: return "WARNING";
        case alignmesh::analysis::Verdict::FAIL:    return "FAIL";
        case alignmesh::analysis::Verdict::INVALID: return "INVALID";
        }
        return "UNKNOWN";
    };

    std::ostringstream o;
    o << "{\n";
    o << "  \"valid\": " << (pkg.valid ? "true" : "false") << ",\n";
    o << "  \"core_version\": \"" << pkg.core_version << "\",\n";
    o << "  \"timestamp\": \"" << pkg.timestamp << "\",\n";
    o << "  \"reference_hash\": \"" << pkg.reference_hash << "\",\n";
    o << "  \"measured_hash\": \"" << pkg.measured_hash << "\",\n";
    o << "  \"verdict\": \"" << verdict_str(pkg.verdict) << "\",\n";
    o << "  \"verdict_label\": \"" << pkg.verdict_label << "\",\n";
    o << "  \"tolerance_mm\": " << pkg.tolerance << ",\n";
    o << "  \"alignment_mode\": \"" << pkg.alignment_mode << "\",\n";
    o << "  \"alignment_rms\": " << pkg.alignment_rms << ",\n";
    o << "  \"precision_tier\": \"" << pkg.precision_tier << "\",\n";
    o << "  \"heatmap_label\": \"" << pkg.heatmap_label << "\",\n";
    o << "  \"stats\": {\n";
    o << "    \"n_points\": " << pkg.unsigned_stats.n_points << ",\n";
    o << "    \"mean\": " << pkg.unsigned_stats.mean << ",\n";
    o << "    \"rms\": " << pkg.unsigned_stats.rms << ",\n";
    o << "    \"max\": " << pkg.unsigned_stats.max << ",\n";
    o << "    \"std_dev\": " << pkg.unsigned_stats.std_dev << ",\n";
    o << "    \"percent_within_tolerance\": " << pkg.unsigned_stats.percent_within_tolerance << "\n";
    o << "  },\n";
    o << "  \"fingerprint\": {\n";
    o << "    \"compiler\": \"" << pkg.fingerprint.compiler_id << " " << pkg.fingerprint.compiler_version << "\",\n";
    o << "    \"cpu\": \"" << pkg.fingerprint.cpu_brand << "\"\n";
    o << "  },\n";
    o << "  \"n_display_points\": " << pkg.points.size() << ",\n";
    o << "  \"heatmap_min\": " << pkg.heatmap_min << ",\n";
    o << "  \"heatmap_max\": " << pkg.heatmap_max << ",\n";
    o << "  \"fully_constrained\": " << (pkg.fully_constrained ? "true" : "false") << ",\n";
    o << "  \"num_under_constrained\": " << pkg.num_under_constrained << ",\n";
    o << "  \"expanded_uncertainty\": " << pkg.expanded_uncertainty << ",\n";
    o << "  \"coverage_factor\": " << pkg.coverage_factor << ",\n";
    o << "  \"uncertainty_established\": " << (pkg.uncertainty_established ? "true" : "false") << ",\n";
    o << "  \"unestablished_contributors\": [";
    for (std::size_t i = 0; i < pkg.unestablished_contributors.size(); ++i) {
        if (i > 0) o << ", ";
        o << "\"" << pkg.unestablished_contributors[i] << "\"";
    }
    o << "],\n";
    o << "  \"acceptance_lower\": " << pkg.acceptance_lower << ",\n";
    o << "  \"acceptance_upper\": " << pkg.acceptance_upper << ",\n";
    o << "  \"transform_matrix\": [";
    for (int i = 0; i < 16; ++i) {
        if (i > 0) o << ", ";
        o << pkg.transform_matrix[i];
    }
    o << "],\n";
    // Per-point deviation array (for large-payload completeness verification).
    o << "  \"point_deviations\": [";
    for (std::size_t i = 0; i < pkg.points.size(); ++i) {
        if (i > 0) o << ",";
        o << pkg.points[i].deviation;
    }
    o << "],\n";
    // Per-point positions (x,y,z triples) for spatial heatmap coloring in the UI.
    o << "  \"point_positions\": [";
    for (std::size_t i = 0; i < pkg.points.size(); ++i) {
        if (i > 0) o << ",";
        o << pkg.points[i].x << "," << pkg.points[i].y << "," << pkg.points[i].z;
    }
    o << "],\n";
    // Checksum: simple sum of all deviation values for integrity verification.
    {
        double cksum = 0;
        for (auto& p : pkg.points) cksum += p.deviation;
        o << "  \"deviation_checksum\": " << cksum << ",\n";
    }
    // Index and value of the max-deviation point.
    {
        double max_dev = 0;
        std::size_t max_idx = 0;
        for (std::size_t i = 0; i < pkg.points.size(); ++i) {
            if (std::abs(pkg.points[i].deviation) > std::abs(max_dev)) {
                max_dev = pkg.points[i].deviation;
                max_idx = i;
            }
        }
        o << "  \"max_deviation_index\": " << max_idx << ",\n";
        o << "  \"max_deviation_value\": " << max_dev << ",\n";
    }
    o << "  \"rps_projected_points\": [";
    for (std::size_t i = 0; i < pkg.rps_projected_points.size(); ++i) {
        if (i > 0) o << ", ";
        auto& pp = pkg.rps_projected_points[i];
        o << "{\"x\":" << pp.x << ",\"y\":" << pp.y << ",\"z\":" << pp.z
          << ",\"valid\":" << (pp.valid ? "true" : "false") << "}";
    }
    o << "],\n";
    o << "  \"warnings\": [";
    for (std::size_t i = 0; i < pkg.warnings.size(); ++i) {
        if (i > 0) o << ", ";
        o << "\"" << pkg.warnings[i] << "\"";
    }
    o << "],\n";
    o << "  \"errors\": [";
    for (std::size_t i = 0; i < pkg.errors.size(); ++i) {
        if (i > 0) o << ", ";
        o << "\"" << pkg.errors[i] << "\"";
    }
    o << "]\n";
    o << "}\n";
    return o.str();
}

} // namespace

int main(int argc, char* argv[]) {
    int port = 8000;
    if (argc > 1) port = std::atoi(argv[1]);

#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
#endif

    WinsockInit wsi;

    socket_t server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "ERROR: cannot create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<unsigned short>(port));

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "ERROR: cannot bind to port " << port << "\n";
        CLOSE_SOCKET(server);
        return 1;
    }

    if (listen(server, 5) == SOCKET_ERROR) {
        std::cerr << "ERROR: listen failed\n";
        CLOSE_SOCKET(server);
        return 1;
    }

    // Find wwwroot directory (next to the executable).
    std::string exe_dir = ".";
#ifdef _WIN32
    {
        char buf[MAX_PATH] = {};
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        std::string exe_path(buf);
        auto last_sep = exe_path.find_last_of("/\\");
        if (last_sep != std::string::npos)
            exe_dir = exe_path.substr(0, last_sep);
    }
#else
    if (argc > 0) {
        std::string exe_path(argv[0]);
        auto last_sep = exe_path.find_last_of("/\\");
        if (last_sep != std::string::npos)
            exe_dir = exe_path.substr(0, last_sep);
    }
#endif
    std::string wwwroot;
    {
        auto try_path = [](const std::string& dir, const std::string& sub) -> std::string {
            auto p = std::filesystem::weakly_canonical(
                std::filesystem::path(dir) / sub / "index.html");
            if (std::filesystem::exists(p))
                return std::filesystem::weakly_canonical(
                    std::filesystem::path(dir) / sub).string();
            return "";
        };
        wwwroot = try_path(exe_dir, "wwwroot");
        if (wwwroot.empty()) wwwroot = try_path(exe_dir, "../wwwroot");
    }

    // Persistent store — STEP files are imported once and reused across requests.
    alignmesh::io::ImmutableSourceStore persistent_store;

    auto fp = alignmesh::numerics::EnvironmentFingerprint::capture();
    std::cout << "=== alignmesh core service v0.1.0 ===" << std::endl;
    std::cout << "Listening on http://localhost:" << port << std::endl;
    std::cout << "Compiler: " << fp.compiler_id << " " << fp.compiler_version << std::endl;
    std::cout << "FP flags: " << ALIGNMESH_FP_FLAGS << std::endl;
    std::cout << "CPU: " << fp.cpu_brand << std::endl;
    std::cout << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET  /health     — service health check" << std::endl;
    std::cout << "  GET  /version    — version + fingerprint" << std::endl;
    std::cout << "  POST /inspect    — run full inspection pipeline" << std::endl;
    std::cout << "    Body: {\"reference\": \"path\", \"measured\": \"path\", \"tolerance\": 0.1}" << std::endl;
    std::cout << "  POST /preview-mesh — tessellate file for 3D preview (returns binary STL)" << std::endl;
    if (!wwwroot.empty())
        std::cout << "  GET  /*          — UI static files from " << wwwroot << std::endl;
    else
        std::cout << "  (no wwwroot found — API only, no UI)" << std::endl;
    std::cout << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        int client_len = sizeof(client_addr);
        socket_t client = accept(server, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client == INVALID_SOCKET) continue;

        std::string req = read_request(client);
        std::string path = extract_path(req);

        std::cout << "[" << path << "] " << std::flush;

        if (req.empty()) {
            CLOSE_SOCKET(client);
            continue;
        }
        if (req.find("OPTIONS") == 0) {
            send_response(client, 200, "text/plain", "");
            std::cout << "CORS preflight" << std::endl;
        }
        else if (path == "/health") {
            send_response(client, 200, "application/json", "{\"status\": \"ok\"}");
            std::cout << "OK" << std::endl;
        }
        else if (path == "/version") {
            std::ostringstream ver;
            ver << "{\n";
            ver << "  \"version\": \"alignmesh 0.1.0\",\n";
            ver << "  \"compiler\": \"" << fp.compiler_id << " " << fp.compiler_version << "\",\n";
            ver << "  \"cpu\": \"" << fp.cpu_brand << "\",\n";
            ver << "  \"fp_flags\": \"" << ALIGNMESH_FP_FLAGS << "\"\n";
            ver << "}\n";
            send_response(client, 200, "application/json", ver.str());
            std::cout << "OK" << std::endl;
        }
        else if (path.find("/upload") == 0 && req.find("POST") == 0) {
            // File upload: accepts raw binary body, saves to a unique temp dir.
            // Returns {"path": "<absolute>", "size": N}.
            std::string filename = "uploaded_part";
            auto qpos = path.find("?filename=");
            if (qpos != std::string::npos) {
                filename = path.substr(qpos + 10);
                // Basic URL decode: %20 -> space (enough for filenames).
                std::string decoded;
                for (std::size_t i = 0; i < filename.size(); ++i) {
                    if (filename[i] == '%' && i + 2 < filename.size()) {
                        int hi = 0;
                        if (std::sscanf(filename.substr(i + 1, 2).c_str(), "%x", &hi) == 1) {
                            decoded += static_cast<char>(hi);
                            i += 2;
                            continue;
                        }
                    }
                    decoded += filename[i];
                }
                filename = decoded;
            }
            // Security: strip path separators, reject traversal.
            for (auto& c : filename) { if (c == '/' || c == '\\') c = '_'; }
            if (filename.find("..") != std::string::npos) filename = "sanitized_upload";
            if (filename.empty()) filename = "uploaded_part";

            std::string body = extract_body(req);

            // Unique subdirectory per upload (atomic counter).
            static std::atomic<int> upload_counter{0};
            int uid = upload_counter.fetch_add(1);
            auto upload_dir = std::filesystem::temp_directory_path() / "alignmesh_uploads"
                              / std::to_string(uid);
            std::filesystem::create_directories(upload_dir);

            auto file_path = upload_dir / filename;
            {
                std::ofstream ofs(file_path, std::ios::binary);
                ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
            }

            std::string abs_path = std::filesystem::absolute(file_path).string();
            // Escape backslashes for JSON.
            std::string json_path;
            for (char c : abs_path) {
                if (c == '\\') json_path += "\\\\";
                else json_path += c;
            }

            send_response(client, 200, "application/json",
                "{\"path\": \"" + json_path + "\", \"size\": " + std::to_string(body.size()) + "}");
            std::cout << "uploaded " << body.size() << " bytes -> " << abs_path << std::endl;
        }
        else if (path.find("/preview-mesh") == 0 && req.find("POST") == 0) {
            // Returns a COARSE binary STL tessellation for 3D preview.
            // For STEP: reads shape, does a fast coarse tessellation (no CadReference).
            // For STL/PLY: returns the mesh as-is.
            std::string body = extract_body(req);
            std::string file_path = json_string(body, "path");
            std::cout << "preview-mesh: " << file_path << " ... " << std::flush;

            if (file_path.empty()) {
                send_response(client, 400, "application/json",
                    "{\"error\": \"missing 'path' in request body\"}");
                std::cout << "ERROR: no path" << std::endl;
            } else {
                try {
                    // Detect format first; for non-STEP, use the fast path.
                    auto bytes = alignmesh::io::read_file(file_path);
                    bool is_step = alignmesh::io::looks_like_step(bytes.data(), bytes.size());

                    std::string stl_bytes;
                    std::size_t tri_count = 0;

                    if (!is_step) {
                        // STL/PLY: import and re-export as binary STL.
                        auto imported = persistent_store.import_file(file_path);
                        stl_bytes = mesh_to_binary_stl(imported.mesh);
                        tri_count = imported.metadata.triangle_count;
                    } else {
                        // STEP: coarse preview tessellation (display-only, ~20x fewer triangles).
                        stl_bytes = coarse_step_preview(file_path);
                        if (!stl_bytes.empty() && stl_bytes.size() >= 84) {
                            uint32_t n; std::memcpy(&n, stl_bytes.data()+80, 4);
                            tri_count = n;
                        }
                    }

                    if (stl_bytes.empty()) {
                        send_response(client, 500, "application/json",
                            "{\"error\": \"tessellation produced empty mesh\"}");
                        std::cout << "ERROR: empty mesh" << std::endl;
                    } else {
                        // Send raw binary STL.
                        std::ostringstream resp;
                        resp << "HTTP/1.1 200 OK\r\n"
                             << "Content-Type: application/octet-stream\r\n"
                             << "Content-Length: " << stl_bytes.size() << "\r\n"
                             << "Access-Control-Allow-Origin: *\r\n"
                             << "Connection: close\r\n"
                             << "\r\n";
                        std::string header = resp.str();
                        send(client, header.c_str(), static_cast<int>(header.size()), 0);
                        const char* data = stl_bytes.data();
                        int remaining = static_cast<int>(stl_bytes.size());
                        while (remaining > 0) {
                            int sent = send(client, data, remaining, 0);
                            if (sent <= 0) break;
                            data += sent;
                            remaining -= sent;
                        }
                        std::cout << "OK (" << tri_count
                                  << " tris, " << stl_bytes.size() << " bytes)" << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::string err = e.what();
                    std::string escaped;
                    for (char c : err) {
                        if (c == '"') escaped += "\\\"";
                        else if (c == '\\') escaped += "\\\\";
                        else escaped += c;
                    }
                    send_response(client, 500, "application/json",
                        "{\"error\": \"" + escaped + "\"}");
                    std::cout << "ERROR: " << err << std::endl;
                }
            }
        }
        else if (path == "/inspect" && req.find("POST") == 0) {
            std::string body = extract_body(req);
            alignmesh::service::InspectionRequest ireq;
            ireq.reference_path = json_string(body, "reference");
            ireq.measured_path = json_string(body, "measured");
            ireq.tolerance_mm = json_number(body, "tolerance");
            if (ireq.tolerance_mm <= 0) ireq.tolerance_mm = 0.1;
            // Parse alignment mode if provided.
            {
                std::string mode = json_string(body, "alignment_mode");
                if (!mode.empty()) ireq.alignment_mode = mode;
            }
            // Parse landmark pairs if provided.
            ireq.landmarks = parse_landmarks(body);
            // Parse RPS datum points if provided.
            ireq.rps_points = parse_rps_points(body);
            // Parse rotation search angle step.
            {
                double as = json_number(body, "angle_step");
                if (as > 0) ireq.angle_step_degrees = as;
            }

            std::cout << "ref=" << ireq.reference_path
                      << " meas=" << ireq.measured_path
                      << " tol=" << ireq.tolerance_mm << "mm ... ";
            std::cout.flush();

            auto pkg = alignmesh::service::run_inspection(ireq, &persistent_store);
            std::string json = result_to_json(pkg);

            send_response(client, 200, "application/json", json);
            std::cout << pkg.verdict_label << std::endl;
        }
        else {
            std::cout << "trying static... wwwroot=" << wwwroot.size() << " " << std::flush;
            if (!wwwroot.empty() && serve_static(client, path, wwwroot)) {
                std::cout << "static OK" << std::endl;
            } else if (!wwwroot.empty() && path.find('.') == std::string::npos) {
                bool ok = serve_static(client, "/index.html", wwwroot);
                std::cout << (ok ? "SPA OK" : "SPA FAIL") << std::endl;
                if (!ok) {
                    send_response(client, 404, "text/plain", "wwwroot index.html not found");
                }
            } else {
                send_response(client, 404, "application/json",
                    "{\"error\": \"final-404\", \"path\": \"" + path + "\", \"wwwroot\": \"" + wwwroot + "\"}");
                std::cout << "404" << std::endl;
            }
        }

        CLOSE_SOCKET(client);
    }

    CLOSE_SOCKET(server);
    return 0;
}
