// Windows SDK headers require this order.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <bcrypt.h>
// clang-format on
#include "core/world_rules.hpp"
#include "editor/voronoi_preview.hpp"
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
std::string fingerprint(std::string_view text) {
    // Conflict detection, not authentication. Token authentication is separate.
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}
std::string revision(const std::filesystem::path& path) {
    return std::filesystem::exists(path) ? fingerprint(read_file(path)) : "missing";
}
Json experiment_settings(const Json& input) {
    if (input.at("format") != "dolbuto-voronoi-experiment" || input.at("version") != 3)
        throw std::runtime_error("보로노이 저장 형식이 올바르지 않습니다.");
    const auto checked = [](const Json& j, const char* key, double lo, double hi, bool whole = false) {
        const auto& v = j.at(key);
        if (!v.is_number())
            throw std::runtime_error(std::string("숫자가 필요합니다: ") + key);
        const double n = v.get<double>();
        if (!std::isfinite(n) || n < lo || n > hi || (whole && n != std::floor(n)))
            throw std::runtime_error(std::string("저장 값 범위 확인: ") + key);
        return v;
    };
    const auto& p = input.at("parameters");
    Json params;
    struct Limit {
        const char* name;
        double lo, hi;
        bool whole{};
    };
    for (const auto& l :
         std::array{Limit{"seed", 0, UINT32_MAX, true}, Limit{"spacing", 512, 32768}, Limit{"jitter", 0, 1},
                    Limit{"warp_strength", 0, 8192}, Limit{"warp_spacing_log2", 2, 17, true},
                    Limit{"warp_octaves", 1, 8, true}, Limit{"warp_gain", 0, 1},
                    Limit{"land_spacing", 512, world_size}, Limit{"land_threshold", -1, 1},
                    Limit{"arch_spacing", 512, world_size}, Limit{"arch_threshold", -1, 1},
                    Limit{"subdivisions", 2, 8, true}, Limit{"island_spacing", 64, world_size},
                    Limit{"island_threshold", -1, 1}, Limit{"arch_edge_fade", 0, .5}})
        params[l.name] = checked(p, l.name, l.lo, l.hi, l.whole);
    for (const auto key : {"warp_enabled", "arch_enabled"}) {
        if (!p.at(key).is_boolean())
            throw std::runtime_error("켜기/끄기 값이 필요합니다.");
        params[key] = p.at(key);
    }
    Json range;
    for (const auto key : {"x0", "z0", "x1", "z1"})
        range[key] = checked(input.at("range"), key, -world_size, 2 * world_size);
    for (const auto axis : {"x", "z"}) {
        const double span =
            range[std::string(axis) + "1"].get<double>() - range[std::string(axis) + "0"].get<double>();
        if (span < 1 || span > world_size)
            throw std::runtime_error("표시 범위 길이는 1~131072입니다.");
    }
    const auto resolution = checked(input, "resolution", 256, 1024, true);
    if (resolution != 256 && resolution != 512 && resolution != 1024)
        throw std::runtime_error("해상도는 256/512/1024 중 하나입니다.");
    Json display =
        input.value("display", Json{{"view", "land"}, {"edges", true}, {"sites", true}, {"auto", true}});
    const std::array views{"land",       "regions", "arch-noise", "island-noise",
                           "land-noise", "cells",   "edges",      "distance"};
    if (!display.at("view").is_string() ||
        std::none_of(views.begin(), views.end(), [&](const char* v) { return display["view"] == v; }))
        throw std::runtime_error("지원하지 않는 지도 표시입니다.");
    for (const auto key : {"edges", "sites", "auto"})
        if (!display.at(key).is_boolean())
            throw std::runtime_error("지도 표시 값은 켜기/끄기여야 합니다.");
    return {{"format", "dolbuto-voronoi-experiment"},
            {"version", 3},
            {"parameters", params},
            {"range", range},
            {"resolution", resolution},
            {"display",
             {{"view", display["view"]},
              {"edges", display["edges"]},
              {"sites", display["sites"]},
              {"auto", display["auto"]}}}};
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
constexpr const char* map_keys[]{"temperature", "precipitation"};
GenerationMap map_kind(const std::string& name) {
    for (int i = 0; i < 2; ++i)
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
    Json config_json(const std::filesystem::path& path) {
        return Json::parse(generation_json(load_generation_config(path)));
    }
    std::filesystem::path workspace_path() const { return root / "worldgen.editor.json"; }
    Json workspace() {
        if (!std::filesystem::exists(workspace_path())) {
            auto path = std::filesystem::exists(draft)       ? draft
                        : std::filesystem::exists(published) ? published
                                                             : root / "assets/worldgen/default.json";
            return {{"schema_version", 1}, {"config", config_json(path)}, {"voronoi", nullptr}};
        }
        auto data = Json::parse(read_file(workspace_path()));
        if (data.at("schema_version") != 1)
            throw std::runtime_error("지원하지 않는 편집기 저장 형식입니다.");
        data["config"] = Json::parse(generation_json(parse_generation_json(data.at("config").dump())));
        if (!data.at("voronoi").is_null()) {
            auto experiment = data.at("voronoi");
            experiment["parameters"]["seed"] = data["config"]["seed"];
            data["voronoi"] = experiment_settings(experiment);
            data["voronoi"]["parameters"].erase("seed");
        }
        return data;
    }
    std::string section_revision(const Json& data, bool climate) {
        auto section = data.at(climate ? "config" : "voronoi");
        if (climate)
            section.erase("seed");
        return fingerprint(section.dump());
    }
    Json workspace_response(const Json& data) {
        auto experiment = data.at("voronoi");
        if (!experiment.is_null())
            experiment["parameters"]["seed"] = data["config"]["seed"];
        return {{"config", data["config"]},
                {"voronoi", experiment},
                {"climate_revision", section_revision(data, true)},
                {"voronoi_revision", section_revision(data, false)},
                {"base_seed", data["config"]["seed"]},
                {"exists", std::filesystem::exists(workspace_path())},
                {"path", utf8_path(workspace_path())}};
    }
    void save_workspace(const Json& data) {
        const auto path = workspace_path();
        auto temporary = path;
        temporary += ".tmp";
        const auto text = data.dump(2) + "\n";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.close();
            if (!file)
                throw std::runtime_error("편집기 작업 파일을 저장하지 못했습니다.");
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("편집기 작업 파일 교체 실패: " + std::to_string(GetLastError()));
    }
    Response route(const Request& req) {
        if (req.headers.find("host") == req.headers.end() || req.headers.at("host") != host)
            return {"잘못된 주소", "text/plain; charset=utf-8", 403};
        if (req.method == "GET" && !req.path.starts_with("/api/")) {
            std::string file = req.path == "/" ? "index.html" : req.path.substr(1);
            if (file != "index.html" && file != "app.js" && file != "style.css" && file != "master-seed.js" &&
                file != "voronoi.html" && file != "voronoi.js" && file != "voronoi.css")
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
        if (req.method == "GET" && req.path == "/api/workspace")
            return json_response(workspace_response(workspace()));
        if (req.method != "POST")
            return {"Not found", "text/plain", 404};
        Json j = Json::parse(req.body);
        if (req.path == "/api/shutdown") {
            stopped = true;
            return json_response({{"message", "편집기를 종료했습니다."}});
        }
        if (req.path == "/api/voronoi/preview") {
            const auto values = sandbox::editor::voronoi_preview(j);
            return {std::string(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float)),
                    "application/octet-stream"};
        }
        if (req.path == "/api/workspace/climate" || req.path == "/api/workspace/voronoi") {
            const bool climate = req.path.ends_with("/climate");
            auto data = workspace();
            auto section = climate
                               ? Json::parse(generation_json(parse_generation_json(j.at("config").dump())))
                               : experiment_settings(j.at("voronoi"));
            const auto seed = climate ? section.at("seed") : section.at("parameters").at("seed");
            if (j.at("revision") != section_revision(data, climate) ||
                (j.at("base_seed") != data["config"]["seed"] && seed != data["config"]["seed"]))
                return {Json({{"error", "다른 창에서 저장한 설정이 있습니다. JSON으로 현재 작업을 보관하고 "
                                        "마지막 저장을 불러와 주세요."}})
                            .dump(),
                        "application/json; charset=utf-8", 409};
            if (!climate)
                section["parameters"].erase("seed");
            data[climate ? "config" : "voronoi"] = std::move(section);
            data["config"]["seed"] = seed;
            save_workspace(data);
            return json_response(workspace_response(data));
        }
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
        if (req.path == "/api/probe") {
            float x = float(wrap_position(number(j, "x", 0, world_size))),
                  z = float(wrap_position(number(j, "z", 0, world_size))), v;
            TerrainGenerator generator(config);
            Json result;
            for (int i = 0; i < 2; ++i) {
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
                                  (L"Local\\DOLBUTOWorldgenEditor-" + std::to_wstring(folder_id)).c_str());
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
        std::cout << "DOLBUTO 월드 생성 편집기 · 게임 실행 불필요\n"
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
