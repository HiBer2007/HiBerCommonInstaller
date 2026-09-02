#include "hci/extension.h"

#include "hci/log.h"
#include "hci/port.h"

#include <libzippp.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace hci {

// ------------------------------------------------------------------
StaticExtensions& StaticExtensions::instance()
{
    static StaticExtensions inst;
    return inst;
}

void StaticExtensions::add(const char* className, ExtensionFactory factory)
{
    for (auto& kv : factories_)
        if (kv.first == className) return;
    factories_.emplace_back(className, std::move(factory));
}

const std::vector<std::pair<std::string, ExtensionFactory>>&
StaticExtensions::all() const
{
    return factories_;
}

// ------------------------------------------------------------------
// SHA-256 (public-domain implementation, adapted from Brad Conte).
// ------------------------------------------------------------------
namespace sha256 {

typedef std::uint32_t word;
typedef std::uint8_t byte;

static const word k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline word rotr(word x, int n) { return (x >> n) | (x << (32 - n)); }

struct Ctx {
    word h[8];
    word total[2];
    word buffer[64];
};

static void init(Ctx& c)
{
    c.h[0] = 0x6a09e667; c.h[1] = 0xbb67ae85;
    c.h[2] = 0x3c6ef372; c.h[3] = 0xa54ff53a;
    c.h[4] = 0x510e527f; c.h[5] = 0x9b05688c;
    c.h[6] = 0x1f83d9ab; c.h[7] = 0x5be0cd19;
    c.total[0] = c.total[1] = 0;
}

static void transform(Ctx& c, const byte* data)
{
    word m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (static_cast<word>(data[j]) << 24) |
               (static_cast<word>(data[j + 1]) << 16) |
               (static_cast<word>(data[j + 2]) << 8) |
               (static_cast<word>(data[j + 3]));
    for (int i = 16; i < 64; ++i) {
        word s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        word s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    word a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3];
    word e = c.h[4], f = c.h[5], g = c.h[6], h = c.h[7];
    for (int i = 0; i < 64; ++i) {
        word S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        word ch = (e & f) ^ (~e & g);
        word temp1 = h + S1 + ch + k[i] + m[i];
        word S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        word maj = (a & b) ^ (a & cc) ^ (b & cc);
        word temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = cc; cc = b; b = a; a = temp1 + temp2;
    }
    c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d;
    c.h[4] += e; c.h[5] += f; c.h[6] += g; c.h[7] += h;
}

static void update(Ctx& c, const byte* data, size_t len)
{
    size_t i = 0;
    while (len >= 64) {
        transform(c, data + i);
        i += 64; len -= 64;
    }
    if (len > 0) {
        size_t idx = static_cast<size_t>(c.total[0] & 63);
        size_t space = 64 - idx;
        size_t copyLen = len < space ? len : space;
        std::memcpy(reinterpret_cast<byte*>(c.buffer) + idx, data + i, copyLen);
    }
    c.total[0] += static_cast<word>(len);
}

static void final(Ctx& c, byte out[32])
{
    size_t idx = static_cast<size_t>(c.total[0] & 63);
    size_t padLen = (idx < 56) ? (56 - idx) : (120 - idx);
    byte pad[128] = {0};
    pad[0] = 0x80;
    std::uint64_t bits = static_cast<std::uint64_t>(c.total[0]) * 8;
    for (int i = 0; i < 8; ++i)
        pad[padLen - 8 + i] = static_cast<byte>(bits >> (56 - 8 * i));
    update(c, pad, padLen);
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<byte>(c.h[i] >> 24);
        out[i * 4 + 1] = static_cast<byte>(c.h[i] >> 16);
        out[i * 4 + 2] = static_cast<byte>(c.h[i] >> 8);
        out[i * 4 + 3] = static_cast<byte>(c.h[i]);
    }
}

} // namespace sha256

namespace {

std::string sha256Hex(const std::vector<char>& data)
{
    unsigned char digest[32];
    sha256::Ctx ctx;
    sha256::init(ctx);
    sha256::update(ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size());
    sha256::final(ctx, digest);
    std::string hex;
    hex.reserve(64);
    static const char* digits = "0123456789abcdef";
    for (unsigned char d : digest) {
        hex += digits[d >> 4];
        hex += digits[d & 0x0f];
    }
    return hex;
}

std::string fileBytesToHex(const std::string& path)
{
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return {};
    std::vector<char> buf((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    return sha256Hex(buf);
}

#ifdef _WIN32
std::wstring s2w(const std::string& s)
{
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n) - 1, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
#endif

} // namespace

// ------------------------------------------------------------------
ExtensionLoader::ExtensionLoader(EventBus* bus, ServiceRegistry* services,
                                 InstallContext* ctx, const ProductConfig* product,
                                 ExtensionRegistry* registry)
    : api_(bus, services, ctx, product, registry)
{
}

ExtensionLoader::~ExtensionLoader()
{
    shutdownAll();
}

void ExtensionLoader::shutdownAll()
{
    // Tear down registries BEFORE unloading extension modules: the registered
    // handlers (std::function) live in the DLL code sections.
    if (api_.hasRegistry()) api_.registry().clear();
    for (auto& e : loaded_) {
        try { e->shutdown(); } catch (...) {}
        e.reset();
    }
    loaded_.clear();
}

void ExtensionLoader::loadStatic()
{
    for (auto& kv : StaticExtensions::instance().all()) {
        IHciExtension* ext = nullptr;
        try {
            ext = kv.second();
        } catch (const std::exception& e) {
            Log::Warn(fmt("static extension '{}' factory threw: {}", kv.first, e.what()));
            continue;
        }
        if (!ext) continue;
        std::shared_ptr<IHciExtension> sp(ext);
        try {
            if (sp->init(api_)) {
                loaded_.push_back(std::move(sp));
                Log::Info(fmt("extension loaded (static): {} v{}", ext->id(), ext->version()));
            } else {
                Log::Warn("extension init failed (static): " + std::string(ext->id()));
            }
        } catch (const std::exception& e) {
            Log::Warn(fmt("extension init threw (static): {}: {}", ext->id(), e.what()));
        }
    }
}

void ExtensionLoader::loadDirectory(const std::string& dir)
{
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(dir), ec)) {
        Log::Warn("extension directory not found: " + dir);
        return;
    }
    std::vector<std::string> dlls, pkgs;
    fs::directory_iterator it(fs::u8path(dir), ec), end;
    for (; it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string extName = it->path().extension().u8string();
        if (extName == ".dll") dlls.push_back(it->path().u8string());
        else if (extName == ".hci") pkgs.push_back(it->path().u8string());
    }
    std::sort(dlls.begin(), dlls.end());
    std::sort(pkgs.begin(), pkgs.end());
    for (auto& d : dlls) {
        std::string err;
        if (!loadDll(d, err)) {
            // Not every DLL is an extension (e.g. runtime DLLs) - warn, keep going.
            Log::Debug(fmt("skip dll ({}): {}", err, d));
        }
    }
    for (auto& p : pkgs) {
        std::string err;
        if (!loadPackage(p, err)) {
            lastError_ = err;
            Log::Warn("package load failed: " + err);
        }
    }
}

std::vector<std::pair<std::string, std::string>> ExtensionLoader::modules() const
{
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(loaded_.size());
    for (auto& e : loaded_) {
        if (e) out.emplace_back(e->id(), e->version());
    }
    return out;
}

bool ExtensionLoader::loadDll(const std::string& dllPath, std::string& err)
{
#ifdef _WIN32
    HMODULE h = ::LoadLibraryW(s2w(dllPath).c_str());
    if (!h) { err = "LoadLibrary failed: " + dllPath; return false; }
    auto fn = reinterpret_cast<IHciExtension* (*)()>(::GetProcAddress(h, "HciGetExtension"));
    if (!fn) {
        err = "HciGetExtension symbol not found in: " + dllPath;
        ::FreeLibrary(h);
        return false;
    }
    IHciExtension* ext = fn();
    if (!ext) {
        err = "HciGetExtension returned null: " + dllPath;
        ::FreeLibrary(h);
        return false;
    }
    std::shared_ptr<IHciExtension> sp(ext, [](IHciExtension* p) { delete p; });
    // NOTE: the module stays loaded until process exit (FreeLibrary skipped on
    // purpose: unloading while std::function registries / CRT state still live
    // in the host caused exit-time access violations in Debug builds).
    if (!sp->init(api_)) {
        err = "init failed: " + std::string(ext->id());
        return false;
    }
    loaded_.push_back(std::move(sp));
    Log::Info(fmt("extension loaded (dll): {} v{} <- {}", ext->id(), ext->version(), dllPath));
    return true;
#else
    void* h = ::dlopen(dllPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) { err = std::string("dlopen failed: ") + ::dlerror(); return false; }
    auto fn = reinterpret_cast<IHciExtension* (*)()>(::dlsym(h, "HciGetExtension"));
    if (!fn) { err = "HciGetExtension not found in: " + dllPath; ::dlclose(h); return false; }
    IHciExtension* ext = fn();
    if (!ext) { err = "HciGetExtension returned null"; ::dlclose(h); return false; }
    std::shared_ptr<IHciExtension> sp(ext, [h](IHciExtension* p) { delete p; ::dlclose(h); });
    if (!sp->init(api_)) { err = "init failed: " + std::string(ext->id()); return false; }
    loaded_.push_back(std::move(sp));
    Log::Info(fmt("extension loaded (dll): {} v{}", ext->id(), ext->version()));
    return true;
#endif
}

bool ExtensionLoader::loadPackage(const std::string& pkgPath, std::string& err)
{
    // .hci = ZIP container: meta.json + dll (+ assets).
    libzippp::ZipArchive za(pkgPath);
    if (!za.open(libzippp::ZipArchive::ReadOnly)) {
        err = "cannot open package: " + pkgPath;
        return false;
    }
    nlohmann::json meta;
    {
        libzippp::ZipEntry me = za.getEntry("meta.json");
        if (me.isNull()) { err = "package missing meta.json: " + pkgPath; za.close(); return false; }
        std::string metaText = me.readAsText();
        meta = nlohmann::json::parse(metaText, nullptr, false);
        if (meta.is_discarded()) { err = "package meta.json invalid"; za.close(); return false; }
    }
    za.close();

    std::string id = meta.value("id", "");
    std::string version = meta.value("version", "");
    std::string dllName = meta.value("dll", "");
    std::string expectedSha = meta.value("sha256", "");
    if (id.empty() || dllName.empty()) {
        err = "package meta.json missing id/dll";
        return false;
    }

    std::string cacheRoot = port::localAppData() + "/hci/ext-cache/" + id + "/" + version;
    std::string dllPath = port::joinPath(cacheRoot, dllName);
    std::error_code ec;
    if (!fs::exists(fs::u8path(dllPath), ec)) {
        fs::remove_all(fs::u8path(cacheRoot), ec);
        if (!fs::create_directories(fs::u8path(cacheRoot), ec)) {
            err = "cannot create cache dir: " + cacheRoot;
            return false;
        }
        if (!za.open(libzippp::ZipArchive::ReadOnly)) {
            err = "cannot reopen package: " + pkgPath;
            return false;
        }
        std::vector<libzippp::ZipEntry> entries = za.getEntries();
        for (auto& e : entries) {
            std::string name = e.getName();
            fs::path dest = fs::u8path(cacheRoot) / fs::u8path(name);
            if (e.isDirectory()) {
                fs::create_directories(dest, ec);
                continue;
            }
            fs::create_directories(dest.parent_path(), ec);
            std::ofstream out(dest, std::ios::binary | std::ios::trunc);
            if (!out) {
                err = "cannot write package entry: " + name;
                za.close();
                return false;
            }
            if (e.readContent(out) < 0) {
                err = "package entry read failed: " + name;
                out.close();
                za.close();
                return false;
            }
            out.close();
        }
        za.close();

        if (!expectedSha.empty()) {
            std::string actual = fileBytesToHex(dllPath);
            if (actual != expectedSha) {
                err = "package sha256 mismatch for " + id + " (expected " +
                      expectedSha + ", got " + actual + ")";
                return false;
            }
        }
    }
    return loadDll(dllPath, err);
}

} // namespace hci