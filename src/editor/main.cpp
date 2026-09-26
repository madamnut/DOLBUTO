// Windows SDK headers require this order.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <bcrypt.h>
// clang-format on
#include "core/world_rules.hpp"
#include "editor/minecraft_reference.hpp"
#include "world/generator.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Json = nlohmann::ordered_json;
using namespace sandbox;
namespace {
constexpr size_t body_limit = 5 * 1024 * 1024;
std::string utf8_path(const std::filesystem::path& path) {
    auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
struct Socket {
    SOCKET handle{INVALID_SOCKET};
    ~Socket() {
        if (handle != INVALID_SOCKET)
            closesocket(handle);
    }
};
std::string read_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("파일을 읽을 수 없습니다: " + utf8_path(path.filename()));
    auto n = f.tellg();
    if (n < 0 || n > 8 * 1024 * 1024)
        throw std::runtime_error("파일 크기를 확인해 주세요.");
    std::string s(static_cast<size_t>(n), '\0');
    f.seekg(0);
    if (!f.read(s.data(), n))
        throw std::runtime_error("파일 읽기 실패");
    return s;
}
std::string revision(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path))
        return "missing";
    // Conflict detection, not authentication. Token authentication is separate.
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : read_file(path)) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}
struct Response {
    std::string body, type{"application/json; charset=utf-8"};
    int status{200};
};
Response json_response(const Json& j) { return {j.dump()}; }
void send_all(SOCKET s, std::string_view data) {
    while (!data.empty()) {
        int n = send(s, data.data(), static_cast<int>(std::min(data.size(), size_t(65536))), 0);
        if (n <= 0)
            return;
        data.remove_prefix(n);
    }
}
void respond(SOCKET s, const Response& r) {
    send_all(s, "HTTP/1.1 " + std::to_string(r.status) + (r.status == 200 ? " OK\r\n" : " Error\r\n") +
                    "Content-Type: " + r.type + "\r\nContent-Length: " + std::to_string(r.body.size()) +
                    "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                    "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; "
                    "img-src 'self' blob: data:; connect-src 'self'; frame-ancestors 'none'\r\n\r\n");
    send_all(s, r.body);
}
struct Request {
    std::string method, path, body;
    std::map<std::string, std::string> headers;
};
Request receive(SOCKET socket) {
    std::string data;
    std::array<char, 8192> buffer{};
    size_t end;
    while ((end = data.find("\r\n\r\n")) == std::string::npos) {
        int n = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (n <= 0)
            throw std::runtime_error("연결이 종료됐습니다.");
        data.append(buffer.data(), n);
        if (data.size() > 16384)
            throw std::runtime_error("요청 헤더가 너무 큽니다.");
    }
    Request r;
    std::istringstream head(data.substr(0, end));
    std::string line, protocol;
    std::getline(head, line);
    std::istringstream first(line);
    first >> r.method >> r.path >> protocol;
    if (protocol != "HTTP/1.1")
        throw std::runtime_error("HTTP/1.1 요청이 필요합니다.");
    while (std::getline(head, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string key = line.substr(0, colon), value = line.substr(colon + 1);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        value.erase(0, value.find_first_not_of(" \t"));
        while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
            value.pop_back();
        if (!r.headers.emplace(key, value).second)
            throw std::runtime_error("중복 요청 헤더");
    }
    if (r.headers.contains("transfer-encoding"))
        throw std::runtime_error("분할 요청은 지원하지 않습니다.");
    size_t length = 0;
    if (r.headers.contains("content-length")) {
        const auto& v = r.headers.at("content-length");
        if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("요청 길이 오류");
        length = std::stoull(v);
    }
    if (length > body_limit)
        throw std::runtime_error("설정 요청은 5 MiB 이하로 보내 주세요.");
    r.body = data.substr(end + 4);
    while (r.body.size() < length) {
        int n =
            recv(socket, buffer.data(), static_cast<int>(std::min(buffer.size(), length - r.body.size())), 0);
        if (n <= 0)
            throw std::runtime_error("요청 본문이 불완전합니다.");
        r.body.append(buffer.data(), n);
    }
    if (r.body.size() != length)
        throw std::runtime_error("요청 길이 불일치");
    return r;
}
constexpr const char* map_keys[]{"groundness",  "smoothness",  "weirdness",     "pv",
                                 "base_height", "temperature", "precipitation", "offset",
                                 "factor",      "jaggedness",  "jagged_noise",  "effective_height"};
GenerationMap map_kind(const std::string& name) {
    for (int i = 0; i < 12; ++i)
        if (name == map_keys[i])
            return static_cast<GenerationMap>(i);
    throw std::runtime_error("알 수 없는 지도 종류");
}
float number(const Json& j, const char* key, float lo, float hi) {
    float v = j.at(key).get<float>();
    if (!std::isfinite(v) || v < lo || v > hi)
        throw std::runtime_error(std::string("범위 확인: ") + key);
    return v;
}
struct Editor {
    std::filesystem::path root, published, draft;
    std::string token, host;
    bool stopped{};
    std::optional<int64_t> minecraft_seed;
    std::unique_ptr<minecraft_reference::Sampler> minecraft_sampler;
    minecraft_reference::Sampler& reference_sampler(const Json& j) {
        const auto seed = j.at("seed").get<std::string>();
        int64_t value{};
        const auto parsed = std::from_chars(seed.data(), seed.data() + seed.size(), value);
        if (seed.empty() || parsed.ec != std::errc{} || parsed.ptr != seed.data() + seed.size())
            throw std::runtime_error("Minecraft 시드는 부호 있는 64비트 정수로 입력해 주세요.");
        if (!minecraft_sampler || minecraft_seed != value) {
            auto next = std::make_unique<minecraft_reference::Sampler>(value);
            minecraft_sampler = std::move(next);
            minecraft_seed = value;
        }
        return *minecraft_sampler;
    }
    // All reference endpoints are computational only. No draft/publish side effects.
    Response reference(const Request& req, const Json& j) {
        namespace mc = minecraft_reference;
        const bool compare = j.value("compare", false);
        std::optional<GenerationConfig> ours;
        if (compare)
            ours = parse_generation_json(j.at("config").dump());
        if (req.path == "/api/minecraft/curve") {
            const int kind = mc::field_index(j.at("kind").get<std::string>());
            if (kind < 7)
                throw std::runtime_error("곡선은 offset/factor/jaggedness만 지원합니다.");
            float c = number(j, "groundness", -2, 2), e = number(j, "smoothness", -2, 2),
                  w = number(j, "weirdness", -2, 2);
            const std::string axis = j.at("axis");
            if (axis != "groundness" && axis != "smoothness" && axis != "weirdness")
                throw std::runtime_error("곡선 축 오류");
            Json points = Json::array();
            for (int i = 0; i <= 256; ++i) {
                const float x = -1.2f + 2.4f * i / 256;
                const float a = axis == "groundness" ? x : c, b = axis == "smoothness" ? x : e,
                            d = axis == "weirdness" ? x : w;
                Json p = {x, mc::spline(kind, a, b, d)};
                if (ours) {
                    const auto& s = kind == 7   ? ours->splines.offset
                                    : kind == 8 ? ours->splines.factor
                                                : ours->splines.jaggedness;
                    p.push_back(s.evaluate(a, b, d));
                }
                points.push_back(std::move(p));
            }
            return json_response({{"points", points}});
        }
        auto& sampler = reference_sampler(j);
        auto coordinate = [&](const char* key) {
            const double v = j.at(key).get<double>();
            if (!std::isfinite(v) || v < -30000000 || v > 30000000)
                throw std::runtime_error("Minecraft 좌표는 -30,000,000~30,000,000입니다.");
            return v;
        };
        std::unique_ptr<TerrainGenerator> generator;
        if (ours)
            generator = std::make_unique<TerrainGenerator>(*ours);
        auto ours_value = [&](int kind, int x, int z) {
            float a = float(wrap_position(x)), b = float(wrap_position(z)), result;
            if (kind >= 7) {
                float c, e, w;
                generator->map(GenerationMap::groundness, std::span(&c, 1), std::span(&a, 1),
                               std::span(&b, 1));
                generator->map(GenerationMap::smoothness, std::span(&e, 1), std::span(&a, 1),
                               std::span(&b, 1));
                generator->map(GenerationMap::weirdness, std::span(&w, 1), std::span(&a, 1),
                               std::span(&b, 1));
                const auto& s = kind == 7   ? ours->splines.offset
                                : kind == 8 ? ours->splines.factor
                                            : ours->splines.jaggedness;
                return s.evaluate(c, e, w);
            }
            generator->map(map_kind(std::string(mc::keys[kind])), std::span(&result, 1), std::span(&a, 1),
                           std::span(&b, 1));
            return result;
        };
        if (req.path == "/api/minecraft/probe") {
            const int x = int(std::floor(coordinate("x"))), z = int(std::floor(coordinate("z")));
            const auto values = sampler.probe(x, z);
            Json result{{"x", x}, {"z", z}, {"minecraft", Json::object()}};
            for (size_t i = 0; i < mc::keys.size(); ++i) {
                result["minecraft"][mc::keys[i]] = values[i];
                if (ours)
                    result["ours"][mc::keys[i]] = ours_value(int(i), x, z);
            }
            result["minecraft_router_offset"] = values[7] + double(-0.50375f);
            return json_response(result);
        }
        if (req.path != "/api/minecraft/preview")
            return {"Not found", "text/plain", 404};
        const int kind = mc::field_index(j.at("kind").get<std::string>());
        const double x0 = coordinate("x0"), z0 = coordinate("z0"), x1 = coordinate("x1"),
                     z1 = coordinate("z1");
        if (x1 <= x0 || z1 <= z0)
            throw std::runtime_error("끝 좌표는 시작 좌표보다 커야 합니다.");
        const int res = j.at("resolution").get<int>();
        if (res < 32 || res > 512)
            throw std::runtime_error("원본 지도 해상도는 32~512입니다.");
        const int width = std::max(2, int(res * (x1 - x0) / std::max(x1 - x0, z1 - z0))),
                  height = std::max(2, int(res * (z1 - z0) / std::max(x1 - x0, z1 - z0)));
        const size_t count = size_t(width) * height;
        std::vector<float> values(2 + count * (compare ? 2 : 1));
        std::vector<float> xs(width), zs(width), ground(width), smooth(width), weird(width);
        std::vector<int> block_x(width);
        for (int col = 0; col < width; ++col) {
            block_x[col] = int(std::floor(x0 + (x1 - x0) * col / (width - 1)));
            xs[col] = float(wrap_position(block_x[col]));
        }
        values[0] = float(width);
        values[1] = float(height);
        for (int row = 0; row < height; ++row) {
            const int z = int(std::floor(z0 + (z1 - z0) * row / (height - 1)));
            if (compare) {
                std::fill(zs.begin(), zs.end(), float(wrap_position(z)));
                auto output = std::span(values).subspan(2 + count + size_t(row) * width, width);
                if (kind < 7)
                    generator->map(map_kind(std::string(mc::keys[kind])), output, xs, zs);
                else {
                    generator->map(GenerationMap::groundness, ground, xs, zs);
                    generator->map(GenerationMap::smoothness, smooth, xs, zs);
                    generator->map(GenerationMap::weirdness, weird, xs, zs);
                    const auto& s = kind == 7   ? ours->splines.offset
                                    : kind == 8 ? ours->splines.factor
                                                : ours->splines.jaggedness;
                    for (int col = 0; col < width; ++col)
                        output[col] = s.evaluate(ground[col], smooth[col], weird[col]);
                }
            }
            for (int col = 0; col < width; ++col) {
                const int x = block_x[col];
                const size_t i = 2 + size_t(row) * width + col;
                values[i] = float(sampler.sample(kind, x, z));
                if (!std::isfinite(values[i]) || (compare && !std::isfinite(values[i + count])))
                    throw std::runtime_error("지도 값이 유한하지 않습니다.");
            }
        }
        return {std::string(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float)),
                "application/octet-stream"};
    }
    Json config_json(const std::filesystem::path& path) {
        return Json::parse(generation_json(load_generation_config(path)));
    }
    Response route(const Request& req) {
        if (req.headers.find("host") == req.headers.end() || req.headers.at("host") != host)
            return {"잘못된 주소", "text/plain; charset=utf-8", 403};
        if (req.method == "GET" && !req.path.starts_with("/api/")) {
            std::string file = req.path == "/" ? "index.html" : req.path.substr(1);
            if (file != "index.html" && file != "app.js" && file != "map-view.js" && file != "style.css" &&
                file != "minecraft.html" && file != "minecraft.js" && file != "minecraft.css")
                return {"Not found", "text/plain", 404};
            return {read_file(root / "assets/editor" / file),
                    file.ends_with(".js")    ? "text/javascript; charset=utf-8"
                    : file.ends_with(".css") ? "text/css; charset=utf-8"
                                             : "text/html; charset=utf-8"};
        }
        if (!req.headers.contains("x-editor-token") || req.headers.at("x-editor-token") != token ||
            (req.headers.contains("origin") && req.headers.at("origin") != "http://" + host))
            return {"연결 인증 실패", "text/plain; charset=utf-8", 403};
        if (req.method == "GET" && req.path == "/api/state") {
            auto path =
                std::filesystem::exists(published) ? published : root / "assets/worldgen/default.json";
            return json_response({{"config", config_json(path)},
                                  {"revision", revision(published)},
                                  {"draft_exists", std::filesystem::exists(draft)},
                                  {"published_path", utf8_path(published)}});
        }
        if (req.method == "GET" && req.path == "/api/draft")
            return json_response({{"config", config_json(draft)}});
        if (req.method != "POST")
            return {"Not found", "text/plain", 404};
        Json j = Json::parse(req.body);
        if (req.path == "/api/shutdown") {
            stopped = true;
            return json_response({{"message", "편집기를 종료했습니다."}});
        }
        if (req.path.starts_with("/api/minecraft/"))
            return reference(req, j);
        auto config = parse_generation_json(j.at("config").dump());
        if (req.path == "/api/validate")
            return json_response({{"config", Json::parse(generation_json(config))},
                                  {"message", "게임 생성기로 설정 검증 완료"}});
        if (req.path == "/api/draft") {
            save_generation_config(draft, config);
            return json_response({{"message", "초안을 저장했습니다. 게임용 확정 파일은 바뀌지 않았습니다."}});
        }
        if (req.path == "/api/publish") {
            if (j.at("revision").get<std::string>() != revision(published))
                return {{Json({{"error", "다른 프로그램이 확정 파일을 변경했습니다. JSON 내보내기로 초안을 "
                                         "보관한 뒤 확정 파일을 다시 불러와 주세요."}})
                             .dump()},
                        "application/json; charset=utf-8",
                        409};
            if (std::filesystem::exists(published)) {
                auto backups = root / "worldgen-backups";
                std::filesystem::create_directories(backups);
                const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
                std::filesystem::copy_file(published,
                                           backups / ("worldgen-" + std::to_string(stamp) + ".json"));
            }
            save_generation_config(published, config);
            return json_response(
                {{"message",
                  "확정 저장 완료. 다음 게임 실행 또는 F8에서 확정 파일을 불러온 뒤 재생성하면 적용됩니다."},
                 {"revision", revision(published)}});
        }
        if (req.path == "/api/preset") {
            std::string name = j.at("name");
            if (name != "original")
                throw std::runtime_error("알 수 없는 프리셋");
            config.splines = default_terrain_splines();
            return json_response({{"config", Json::parse(generation_json(config))}});
        }
        if (req.path == "/api/curve") {
            Json values = Json::array();
            std::string kind = j.at("kind");
            if (kind != "offset" && kind != "factor" && kind != "jaggedness")
                throw std::runtime_error("알 수 없는 곡선");
            float c = j.value("groundness", 0.0f), e = j.value("smoothness", 0.0f);
            const auto& grid = kind == "factor"       ? config.splines.factor
                               : kind == "jaggedness" ? config.splines.jaggedness
                                                      : config.splines.offset;
            std::optional<TerrainSpline> local;
            Json controls = Json::array();
            float xmin = -1, xmax = 1;
            if (j.contains("node_path")) {
                const int cell = j.at("cell").get<int>();
                if (!grid.tree && (cell < 0 || size_t(cell) >= grid.cells.size() || !grid.cells[cell]))
                    throw std::runtime_error("선택한 칸에 곡선이 없습니다.");
                const auto path = j.at("node_path").get<std::vector<int>>();
                if (path.size() > 8)
                    throw std::runtime_error("곡선 경로가 너무 깊습니다.");
                const TerrainSpline* node = grid.tree ? &*grid.tree : &*grid.cells[cell];
                for (const int index : path) {
                    if (index < 0 || size_t(index) >= node->values.size())
                        throw std::runtime_error("잘못된 하위 곡선 경로입니다.");
                    node = &node->values[index];
                }
                local = *node;
                // Show this node's own input axis. Descendants are sampled at an explicit W slice.
                const float weird = float(number(j, "preview_weirdness", -2, 2));
                if (local->axis != SplineAxis::constant) {
                    local->axis = SplineAxis::weirdness;
                    xmin = std::min(xmin, local->locations.front());
                    xmax = std::max(xmax, local->locations.back());
                    for (size_t i = 0; i < local->values.size(); ++i) {
                        const float value = local->values[i].evaluate(c, e, weird);
                        controls.push_back({local->locations[i], value});
                        local->values[i] = TerrainSpline{};
                        local->values[i].constant = value;
                    }
                }
            }
            const auto slice = j.value("axis", std::string("weirdness"));
            const float fixed_w = j.value("preview_weirdness", 0.0f);
            for (int i = 0; i <= 256; ++i) {
                float x = xmin + (xmax - xmin) * i / 256;
                float y = local ? local->evaluate(x)
                                : grid.evaluate(slice == "groundness" ? x : c, slice == "smoothness" ? x : e,
                                                slice == "weirdness" ? x : fixed_w);
                if (!std::isfinite(y))
                    throw std::runtime_error("곡선 계산 결과가 유한하지 않습니다.");
                values.push_back({x, y});
            }
            return json_response({{"points", values}, {"controls", controls}});
        }
        if (req.path == "/api/probe") {
            float x = float(wrap_position(number(j, "x", 0, world_size))),
                  z = float(wrap_position(number(j, "z", 0, world_size))), v;
            TerrainGenerator generator(config);
            Json result;
            for (int i = 0; i < 12; ++i) {
                generator.map(static_cast<GenerationMap>(i), std::span(&v, 1), std::span(&x, 1),
                              std::span(&z, 1));
                result[map_keys[i]] = v;
            }
            return json_response(result);
        }
        if (req.path == "/api/preview") {
            auto kind = map_kind(j.at("kind"));
            const float x0 = number(j, "x0", 0, world_size), z0 = number(j, "z0", 0, world_size),
                        x1 = number(j, "x1", 0, world_size), z1 = number(j, "z1", 0, world_size);
            if (x1 <= x0 || z1 <= z0)
                throw std::runtime_error("미리보기 끝 좌표는 시작 좌표보다 커야 합니다.");
            int res = j.at("resolution").get<int>();
            if (res < 32 || res > 1024)
                throw std::runtime_error("해상도는 32~1024입니다.");
            if (j.value("before_warp", false))
                disable_warps(config);
            float contribution = 1;
            int octave = j.value("octave", -1);
            if (octave >= 0) {
                if (kind != GenerationMap::groundness || octave >= config.groundness.octaves)
                    throw std::runtime_error("Groundness 옥타브를 확인해 주세요.");
                config.groundness.preview_octave = octave;
                config.groundness.preview_weighted = j.value("weighted", false);
            }
            TerrainGenerator generator(config);
            int w = std::max(2, int(res * (x1 - x0) / std::max(x1 - x0, z1 - z0))),
                h = std::max(2, int(res * (z1 - z0) / std::max(x1 - x0, z1 - z0)));
            std::vector<float> values(size_t(w) * h + 2), xs(w), zs(w);
            values[0] = float(w);
            values[1] = float(h);
            for (int x = 0; x < w; ++x)
                xs[x] = float(wrap_position(x0 + double(x1 - x0) * x / (w - 1)));
            for (int z = 0; z < h; ++z) {
                std::fill(zs.begin(), zs.end(), float(wrap_position(z0 + double(z1 - z0) * z / (h - 1))));
                auto row = std::span(values).subspan(2 + size_t(z) * w, w);
                generator.map(kind, row, xs, zs);
                for (float& v : row) {
                    v *= contribution;
                    if (!std::isfinite(v))
                        throw std::runtime_error("지도 계산 결과가 유한하지 않습니다.");
                }
            }
            return {std::string(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float)),
                    "application/octet-stream"};
        }
        return {"Not found", "text/plain", 404};
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        SetConsoleOutputCP(CP_UTF8);
        std::array<wchar_t, 32768> executable{};
        auto count = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
        if (!count || count >= executable.size())
            throw std::runtime_error("실행 경로 확인 실패");
        Editor editor;
        editor.root = std::filesystem::path(executable.data()).parent_path();
        bool browser = true;
        int port = 0;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--no-browser")
                browser = false;
            else if (arg == "--port" && i + 1 < argc)
                port = std::stoi(argv[++i]);
            else
                throw std::runtime_error("Usage: worldgen_editor [--no-browser] [--port 0..65535]");
        }
        if (port < 0 || port > 65535)
            throw std::runtime_error("포트 범위 오류");
        editor.published = editor.root / "worldgen.json";
        editor.draft = editor.root / "worldgen.draft.json";
        std::array<unsigned char, 24> entropy{};
        if (BCryptGenRandom(nullptr, entropy.data(), ULONG(entropy.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) <
            0)
            throw std::runtime_error("토큰 생성 실패");
        constexpr char hex[] = "0123456789abcdef";
        for (auto b : entropy) {
            editor.token += hex[b >> 4];
            editor.token += hex[b & 15];
        }
        // One editor per game folder prevents two publishers racing their backups/atomic saves.
        uint64_t folder_id = 14695981039346656037ULL;
        for (wchar_t c : editor.root.wstring()) {
            folder_id ^= uint64_t(std::towlower(c));
            folder_id *= 1099511628211ULL;
        }
        auto mutex = CreateMutexW(nullptr, FALSE,
                                  (L"Local\\SandboxWorldgenEditor-" + std::to_wstring(folder_id)).c_str());
        if (!mutex)
            throw std::runtime_error("편집기 잠금 생성 실패");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex);
            throw std::runtime_error("이 폴더의 편집기가 이미 실행 중입니다.");
        }
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data))
            throw std::runtime_error("로컬 통신 초기화 실패");
        Socket server{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        if (server.handle == INVALID_SOCKET)
            throw std::runtime_error("소켓 생성 실패");
        BOOL exclusive = TRUE;
        setsockopt(server.handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive),
                   sizeof(exclusive));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<u_short>(port));
        if (bind(server.handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
            listen(server.handle, 8))
            throw std::runtime_error("로컬 주소를 열지 못했습니다.");
        int length = sizeof(address);
        getsockname(server.handle, reinterpret_cast<sockaddr*>(&address), &length);
        editor.host = "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        const auto url = "http://" + editor.host + "/#" + editor.token;
        std::cout << "월드 생성 편집기 · 게임 실행 불필요\n"
                  << url << "\n이 창을 닫으면 편집기가 종료됩니다.\n"
                  << std::flush;
        if (browser)
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        while (!editor.stopped) {
            Socket client{accept(server.handle, nullptr, nullptr)};
            if (client.handle == INVALID_SOCKET)
                break;
            DWORD timeout = 5000;
            setsockopt(client.handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout),
                       sizeof(timeout));
            setsockopt(client.handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout),
                       sizeof(timeout));
            try {
                respond(client.handle, editor.route(receive(client.handle)));
            } catch (const std::exception& e) {
                respond(client.handle,
                        {Json({{"error", e.what()}}).dump(), "application/json; charset=utf-8", 400});
            }
        }
        CloseHandle(mutex);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
