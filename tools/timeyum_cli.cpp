// Command line front end for the Timeyum core.
//   timeyum_cli in.ppm out.ppm length=0.8 profile=camera timing_shift=100 frame=12 fps=24
//   timeyum_cli in.ppm out.ppm warp=1 prev=f0011.ppm,f0010.ppm frame=12      (warp reaction to the video)
//   timeyum_cli --seq in_%04d.ppm out_%04d.ppm 1 48 warp=1 fps=24            (a sequence, history is automatic)
//   timeyum_cli in.ppm out.ppm control_file=matte.ppm control=luma control_matte=1     (a matte, alpha or depth map steers it)
//   control_file may contain %04d in --seq mode. control= is one of off, luma, alpha, red, green, blue, depth.
//   The control picture is read as it is, without any colour conversion.
// Formats: .ppm (8 bit RGB), .pam (8 bit RGB or RGBA), .pfm (float RGB), .tyf (float, any 3-4 channels).
// 8 bit files are read as sRGB, float files as linear, unless space= is given.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "../core/Parallel.h"
#include "../core/Timeyum.h"

using namespace timeyum;

namespace {

bool fail(const std::string& msg) {
    std::cerr << "timeyum_cli: " << msg << "\n";
    return false;
}

std::string ext(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    std::string e = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

// Reads the next whitespace separated token of a PNM style header, skipping # comments.
bool token(std::istream& in, std::string& out) {
    out.clear();
    char c;
    while (in.get(c)) {
        if (c == '#') {
            while (in.get(c) && c != '\n') {}
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(c);
            while (in.get(c) && !std::isspace(static_cast<unsigned char>(c))) out.push_back(c);
            return true;
        }
    }
    return false;
}

bool readImage(const std::string& path, Image& img, bool& isFloat) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("cannot open " + path);
    const std::string e = ext(path);
    std::string t;
    if (e == "tyf") {
        int w, h, c;
        std::string magic;
        if (!(in >> magic >> w >> h >> c) || magic != "TYF1") return fail("bad TYF header");
        in.get();
        img = Image(w, h, c);
        in.read(reinterpret_cast<char*>(img.data.data()), img.data.size() * sizeof(float));
        isFloat = true;
        return static_cast<bool>(in);
    }
    if (!token(in, t)) return fail("empty file");
    if (t == "P6" || t == "P7") {
        int w = 0, h = 0, c = 3, maxv = 255;
        if (t == "P6") {
            std::string a, b, m;
            if (!token(in, a) || !token(in, b) || !token(in, m)) return fail("bad PPM header");
            w = std::atoi(a.c_str());
            h = std::atoi(b.c_str());
            maxv = std::atoi(m.c_str());
        } else {
            std::string k;
            while (token(in, k) && k != "ENDHDR") {
                std::string v;
                token(in, v);
                if (k == "WIDTH") w = std::atoi(v.c_str());
                else if (k == "HEIGHT") h = std::atoi(v.c_str());
                else if (k == "DEPTH") c = std::atoi(v.c_str());
                else if (k == "MAXVAL") maxv = std::atoi(v.c_str());
            }
        }
        // token() already consumed the single whitespace byte that ends the header
        if (maxv != 255 || w <= 0 || h <= 0 || c < 3 || c > 4) return fail("only 8 bit RGB/RGBA PNM is supported");
        std::vector<unsigned char> raw(static_cast<size_t>(w) * h * c);
        in.read(reinterpret_cast<char*>(raw.data()), raw.size());
        if (!in) return fail("truncated file");
        img = Image(w, h, c);
        for (size_t i = 0; i < raw.size(); ++i) img.data[i] = raw[i] / 255.0f;
        isFloat = false;
        return true;
    }
    if (t == "PF") {
        std::string a, b, s;
        if (!token(in, a) || !token(in, b) || !token(in, s)) return fail("bad PFM header");
        const int w = std::atoi(a.c_str()), h = std::atoi(b.c_str());
        if (std::atof(s.c_str()) > 0) return fail("big endian PFM is not supported");
        img = Image(w, h, 3);
        for (int y = h - 1; y >= 0; --y) in.read(reinterpret_cast<char*>(img.row(y)), sizeof(float) * w * 3);
        isFloat = true;
        return static_cast<bool>(in);
    }
    return fail("unknown format (use ppm, pam, pfm or tyf)");
}

bool writeImage(const std::string& path, const Image& img, bool isFloat) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return fail("cannot write " + path);
    const std::string e = ext(path);
    const int w = img.width, h = img.height, c = img.channels;
    if (e == "tyf") {
        out << "TYF1 " << w << " " << h << " " << c << "\n";
        out.write(reinterpret_cast<const char*>(img.data.data()), img.data.size() * sizeof(float));
        return true;
    }
    if (e == "pfm") {
        if (c < 3) return fail("PFM needs 3 channels");
        out << "PF\n" << w << " " << h << "\n-1.0\n";
        std::vector<float> row(static_cast<size_t>(w) * 3);
        for (int y = h - 1; y >= 0; --y) {
            for (int x = 0; x < w; ++x)
                for (int k = 0; k < 3; ++k) row[static_cast<size_t>(x) * 3 + k] = img.row(y)[static_cast<size_t>(x) * c + k];
            out.write(reinterpret_cast<const char*>(row.data()), row.size() * sizeof(float));
        }
        return true;
    }
    const int oc = (e == "pam" && c >= 4) ? 4 : 3;
    if (e == "pam") out << "P7\nWIDTH " << w << "\nHEIGHT " << h << "\nDEPTH " << oc << "\nMAXVAL 255\nTUPLTYPE " << (oc == 4 ? "RGB_ALPHA" : "RGB") << "\nENDHDR\n";
    else out << "P6\n" << w << " " << h << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(w) * oc);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < oc; ++k) {
                const float v = img.row(y)[static_cast<size_t>(x) * c + k];
                row[static_cast<size_t>(x) * oc + k] = static_cast<unsigned char>(std::min(std::max(v, 0.f), 1.f) * 255.f + 0.5f);
            }
        out.write(reinterpret_cast<const char*>(row.data()), row.size());
    }
    (void)isFloat;
    return true;
}

using Setter = std::function<bool(Params&, const std::string&)>;

bool lookupEnum(const std::string& s, std::initializer_list<const char*> names, int& out) {
    int i = 0;
    for (const char* n : names) {
        if (s == n) { out = i; return true; }
        ++i;
    }
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end && *end == '\0' && !s.empty()) { out = static_cast<int>(v); return true; }
    return false;
}

std::vector<double> numbers(const std::string& s) {
    std::vector<double> v;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) v.push_back(std::atof(part.c_str()));
    return v;
}

std::map<std::string, Setter> buildSetters() {
    std::map<std::string, Setter> m;
#define REAL(name) m[#name] = [](Params& p, const std::string& v) { p.name = std::atof(v.c_str()); return true; }
#define INT(name) m[#name] = [](Params& p, const std::string& v) { p.name = std::atoi(v.c_str()); return true; }
#define BOOL(name) m[#name] = [](Params& p, const std::string& v) { p.name = v == "1" || v == "true" || v == "on"; return true; }
    REAL(opacity); BOOL(affect_alpha); REAL(length); REAL(angle); BOOL(symmetric); REAL(back_length);
    REAL(start_offset); REAL(frame_gap); REAL(falloff); REAL(falloff_curve); REAL(decay);
    REAL(timing_shift); REAL(shutter_angle); REAL(pulldown_angle); REAL(claw_ease);
    REAL(smear); REAL(gain); REAL(threshold); REAL(knee); REAL(cleanup);
    BOOL(ghost); INT(ghost_count); REAL(ghost_offset); REAL(ghost_strength); REAL(ghost_decay); REAL(ghost_length);
    REAL(roll); REAL(roll_bar); REAL(roll_bar_soft); REAL(saturation); REAL(chroma); REAL(breakup); REAL(breakup_scale);
    BOOL(shake); REAL(shake_amount); REAL(shake_freq); REAL(shake_smooth); INT(shake_seed); REAL(shake_length);
    REAL(shake_angle); REAL(shake_smear); REAL(shake_timing); REAL(shake_roll); REAL(shake_weave_x); REAL(shake_weave_y);
    REAL(shake_ghost); REAL(pixel_scale);
    BOOL(warp); REAL(warp_amount); REAL(flow_length); REAL(flow_wave); REAL(flow_scale); REAL(flow_speed); INT(flow_detail);
    INT(flow_seed); REAL(drift_angle); REAL(drift_speed); REAL(luma_response); REAL(luma_softness); REAL(motion_response);
    REAL(motion_sensitivity); REAL(inertia); INT(history); REAL(length_reaction); REAL(wave_reaction); REAL(pull_x);
    REAL(pull_y); REAL(pull_strength); REAL(pull_radius); REAL(pull_length); REAL(auto_strength); REAL(base_follow);
    INT(warp_levels);
    BOOL(control_invert); REAL(control_black); REAL(control_white); REAL(control_near); REAL(control_far);
    REAL(control_softness); REAL(control_matte); REAL(control_emit); REAL(control_length); REAL(control_warp); BOOL(control_view);
#undef REAL
#undef INT
#undef BOOL
    m["profile"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"fade", "exponential", "camera", "curve"}, p.profile); };
    m["edge"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"wrap", "extend", "mirror", "black"}, p.edge); };
    m["blend"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"exposure", "add", "screen", "lighten"}, p.blend); };
    m["space"] = m["colorspace"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"linear", "srgb", "gamma24", "logc3", "slog3"}, p.colorspace); };
    m["control"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"off", "luma", "alpha", "red", "green", "blue", "depth"}, p.control); };
    m["warp_view"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"result", "length", "reaction", "displacement"}, p.warp_view); };
    m["curve"] = [](Params& p, const std::string& v) { p.curve = numbers(v); return true; };
    m["tint"] = [](Params& p, const std::string& v) {
        const auto n = numbers(v);
        if (n.size() != 3) return false;
        for (int i = 0; i < 3; ++i) p.tint[i] = n[i];
        return true;
    };
    return m;
}

}  // namespace

bool applyArgs(const std::map<std::string, Setter>& setters, int argc, char** argv, int first, Params& p, double& frame,
               double& fps, bool& spaceGiven, std::vector<std::string>& prev, std::string& controlFile) {
    for (int i = first; i < argc; ++i) {
        const std::string arg = argv[i];
        const size_t eq = arg.find('=');
        if (eq == std::string::npos) return fail("expected key=value: " + arg);
        const std::string k = arg.substr(0, eq), v = arg.substr(eq + 1);
        if (k == "frame") frame = std::atof(v.c_str());
        else if (k == "fps") fps = std::atof(v.c_str());
        else if (k == "threads") setMaxThreads(std::atoi(v.c_str()));
        else if (k == "control_file") controlFile = v;
        else if (k == "prev") {
            std::stringstream ss(v);
            std::string part;
            while (std::getline(ss, part, ',')) prev.push_back(part);
        } else {
            const auto it = setters.find(k);
            if (it == setters.end() || !it->second(p, v)) return fail("unknown or invalid parameter: " + arg);
            if (k == "space" || k == "colorspace") spaceGiven = true;
        }
    }
    return true;
}

std::string numbered(const std::string& pattern, int n) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), pattern.c_str(), n);
    return buf;
}

int main(int argc, char** argv) {
    const auto setters = buildSetters();
    if (argc >= 2 && std::string(argv[1]) == "--list") {
        for (const auto& kv : setters) std::cout << kv.first << "\n";
        std::cout << "frame\nfps\nthreads\nprev\ncontrol_file\n";
        return 0;
    }
    const bool seq = argc >= 2 && std::string(argv[1]) == "--seq";
    if ((!seq && argc < 3) || (seq && argc < 6)) {
        std::cerr << "usage: timeyum_cli in out [key=value ...] | --seq in_%04d out_%04d first last [key=value ...] | --list\n";
        return 2;
    }
    Params p;
    double frame = 0.0, fps = 24.0;
    bool spaceGiven = false;
    std::vector<std::string> prev;
    std::string controlFile;
    if (!applyArgs(setters, argc, argv, seq ? 6 : 3, p, frame, fps, spaceGiven, prev, controlFile)) return 2;
    if (p.control != kCtlOff && controlFile.empty()) return fail("control= needs control_file=a picture"), 2;

    if (!seq) {
        Image src, dst;
        bool isFloat = false;
        if (!readImage(argv[1], src, isFloat)) return 1;
        if (!spaceGiven) p.colorspace = isFloat ? kCsLinear : kCsSrgb;
        std::vector<LumaGrid> history;
        for (const std::string& path : prev) {
            Image h;
            bool f = false;
            if (!readImage(path, h, f)) return 1;
            history.push_back(h.width == src.width && h.height == src.height ? makeLumaGrid(h, p.colorspace) : LumaGrid());
        }
        Image ctl;
        bool ctlFloat = false;
        if (p.control != kCtlOff && !readImage(controlFile, ctl, ctlFloat)) return 1;
        process(p, src, dst, frame, fps, &history, p.control != kCtlOff ? &ctl : nullptr);
        return writeImage(argv[2], dst, isFloat) ? 0 : 1;
    }

    const std::string inPattern = argv[2], outPattern = argv[3];
    const int first = std::atoi(argv[4]), last = std::atoi(argv[5]);
    const int need = warpHistoryCount(p);
    std::map<int, LumaGrid> grids;
    auto gridOf = [&](int n, int colorspace, int w, int h) -> LumaGrid {
        auto it = grids.find(n);
        if (it != grids.end()) return it->second;
        Image img;
        bool f = false;
        LumaGrid g;
        {
            std::ifstream probe(numbered(inPattern, n));
            if (probe && readImage(numbered(inPattern, n), img, f) && img.width == w && img.height == h) g = makeLumaGrid(img, colorspace);
        }
        grids[n] = g;
        return g;
    };
    for (int n = first; n <= last; ++n) {
        Image src, dst;
        bool isFloat = false;
        if (!readImage(numbered(inPattern, n), src, isFloat)) return 1;
        Params pf = p;
        if (!spaceGiven) pf.colorspace = isFloat ? kCsLinear : kCsSrgb;
        std::vector<LumaGrid> history;
        for (int j = 1; j <= need; ++j) history.push_back(gridOf(n - j, pf.colorspace, src.width, src.height));
        Image ctl;
        bool ctlFloat = false;
        if (p.control != kCtlOff) {
            const std::string file = controlFile.find('%') != std::string::npos ? numbered(controlFile, n) : controlFile;
            if (!readImage(file, ctl, ctlFloat)) return 1;
        }
        process(pf, src, dst, n, fps, &history, p.control != kCtlOff ? &ctl : nullptr);
        if (!writeImage(numbered(outPattern, n), dst, isFloat)) return 1;
        grids.erase(n - need - 1);
    }
    return 0;
}
