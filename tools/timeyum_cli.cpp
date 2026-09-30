// Command line front end for the Timeyum core.
//   timeyum_cli in.ppm out.ppm length=0.8 profile=camera timing_shift=100 frame=12 fps=24
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
        in.get();
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
        in.get();
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
#undef REAL
#undef INT
#undef BOOL
    m["profile"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"fade", "exponential", "camera", "curve"}, p.profile); };
    m["edge"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"wrap", "extend", "mirror", "black"}, p.edge); };
    m["blend"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"exposure", "add", "screen", "lighten"}, p.blend); };
    m["space"] = m["colorspace"] = [](Params& p, const std::string& v) { return lookupEnum(v, {"linear", "srgb", "gamma24", "logc3", "slog3"}, p.colorspace); };
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

int main(int argc, char** argv) {
    const auto setters = buildSetters();
    if (argc >= 2 && std::string(argv[1]) == "--list") {
        for (const auto& kv : setters) std::cout << kv.first << "\n";
        std::cout << "frame\nfps\nthreads\n";
        return 0;
    }
    if (argc < 3) {
        std::cerr << "usage: timeyum_cli in out [key=value ...] | --list\n";
        return 2;
    }
    Params p;
    double frame = 0.0, fps = 24.0;
    bool spaceGiven = false;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        const size_t eq = arg.find('=');
        if (eq == std::string::npos) return fail("expected key=value: " + arg), 2;
        const std::string k = arg.substr(0, eq), v = arg.substr(eq + 1);
        if (k == "frame") frame = std::atof(v.c_str());
        else if (k == "fps") fps = std::atof(v.c_str());
        else if (k == "threads") setMaxThreads(std::atoi(v.c_str()));
        else {
            const auto it = setters.find(k);
            if (it == setters.end() || !it->second(p, v)) return fail("unknown or invalid parameter: " + arg), 2;
            if (k == "space" || k == "colorspace") spaceGiven = true;
        }
    }
    Image src, dst;
    bool isFloat = false;
    if (!readImage(argv[1], src, isFloat)) return 1;
    if (!spaceGiven) p.colorspace = isFloat ? kCsLinear : kCsSrgb;
    process(p, src, dst, frame, fps);
    return writeImage(argv[2], dst, isFloat) ? 0 : 1;
}
