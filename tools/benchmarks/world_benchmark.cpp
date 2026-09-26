// Explicit CPU pipeline measurement. No CTest, synthetic input, or saved-setting mutation.
#include "world/profiling.hpp"
#include "world/stream.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace sandbox;
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 5)
            throw std::runtime_error(
                "Usage: world_benchmark worldgen.json output_directory [radius=6] [repetitions=7]");
        const int radius = argc >= 4 ? std::stoi(argv[3]) : 6;
        const int repetitions = argc >= 5 ? std::stoi(argv[4]) : 7;
        if (radius < 1 || radius > 64 || repetitions < 1 || repetitions > 100)
            throw std::runtime_error("Radius must be 1..64 and repetitions 1..100");
        auto generator = std::make_shared<TerrainGenerator>(load_generation_config(argv[1]));
        const std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        struct Site {
            ColumnKey key;
            float height, relief;
        };
        std::vector<Site> sites;
        for (int z = 0; z < 32; ++z)
            for (int x = 0; x < 32; ++x) {
                const int bx = 2048 + x * 4096, bz = 2048 + z * 4096;
                std::array<float, 5> xs{float(bx), float(bx - 64), float(bx + 64), float(bx), float(bx)};
                std::array<float, 5> zs{float(bz), float(bz), float(bz), float(bz - 64), float(bz + 64)}, h{},
                    s{};
                generator->profile(h, s, xs, zs);
                sites.push_back(
                    {{bx / 16, bz / 16},
                     h[0],
                     *std::max_element(h.begin(), h.end()) - *std::min_element(h.begin(), h.end())});
            }
        const auto low =
            *std::min_element(sites.begin(), sites.end(), [](auto a, auto b) { return a.height < b.height; });
        const auto high =
            *std::max_element(sites.begin(), sites.end(), [](auto a, auto b) { return a.relief < b.relief; });
        const auto land = *std::min_element(sites.begin(), sites.end(), [](auto a, auto b) {
            return std::abs(a.height - 220) + a.relief < std::abs(b.height - 220) + b.relief;
        });
        std::array<Site, 3> areas{low, land, high};
        std::ofstream selection(out / "areas.csv");
        selection << "area,column_x,column_z,height,relief\n";
        for (int i = 0; i < 3; ++i) {
            auto a = areas[i];
            selection << i << ',' << a.key.x << ',' << a.key.z << ',' << a.height << ',' << a.relief << '\n';
            std::cout << "area=" << i << " origin=" << a.key.x << ',' << a.key.z << " height=" << a.height
                      << " relief=" << a.relief << std::endl;
        }
        std::ofstream summary(out / "runs.csv");
        summary << "run,area,instrumented,radius,columns,faces,elapsed_ms\n" << std::setprecision(12);
        // Both modes use identical compiled algorithms. Alternate order to expose measurement overhead/drift.
        for (int run = 0; run < repetitions; ++run)
            for (int ai = 0; ai < 3; ++ai)
                for (int m = 0; m < 2; ++m) {
                    const int area = (ai + run) % 3;
                    const bool detailed = (m + run) % 2 == 0;
                    profiling::enabled = false;
                    if (detailed)
                        profiling::start();
                    uint64_t faces = 0;
                    size_t columns = 0;
                    const auto start = std::chrono::steady_clock::now();
                    {
                        WorldStream stream(generator);
                        stream.request(areas[area].key, radius);
                        std::optional<ColumnKey> awaiting_publication;
                        while (stream.pending()) {
                            if (awaiting_publication) {
                                if (stream.publish(*awaiting_publication)) {
                                    awaiting_publication.reset();
                                    ++columns;
                                } else
                                    std::this_thread::yield();
                            } else if (auto ready = stream.take_ready()) {
                                for (const auto& mesh : ready->meshes)
                                    faces += mesh.faces.size() + mesh.ice.size() + mesh.water.size();
                                // publish uses try_lock: contention requires a later retry, like WorldView.
                                awaiting_publication = ready->data.key;
                            } else
                                std::this_thread::sleep_for(std::chrono::microseconds(100));
                            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(90))
                                throw std::runtime_error("Measurement timeout");
                        }
                    }
                    const double ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                            .count();
                    summary << run << ',' << area << ',' << detailed << ',' << radius << ',' << columns << ','
                            << faces << ',' << ms << '\n';
                    summary.flush();
                    if (detailed)
                        profiling::save(out /
                                        ("cpu-" + std::to_string(run) + "-" + std::to_string(area) + ".csv"));
                    std::cout << "run=" << run << " area=" << area << " profile=" << detailed
                              << " columns=" << columns << " ms=" << ms << std::endl;
                }
        profiling::enabled = false;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
