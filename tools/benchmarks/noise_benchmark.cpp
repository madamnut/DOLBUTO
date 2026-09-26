// Explicit standalone performance measurement, not a regression test or game startup path.
#include "psrd/inspection.hpp"
#include "psrd/psrd.hpp"
#include "world/periodic_perlin.hpp"
#include <FastNoise/FastNoise.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <intrin.h>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>
#include <windows.h>

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto feature = FastSIMD::FeatureSet::AVX2;
constexpr float period = 131072.0f, tau = 6.2831853071795864769f;
volatile double observable{};
constexpr std::array<const char*, 6> names{
    "perlin2d", "periodic_perlin2d", "simplex4d_cached", "simplex4d_torus_grid", "simplex4d_torus_point",
    "psrd2d"};

void pin_cpu() {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    std::vector<std::byte> data(bytes);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()),
            &bytes))
        throw std::runtime_error("CPU topology unavailable");
    DWORD_PTR process_mask{}, system_mask{}, selected{};
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
        throw std::runtime_error("CPU affinity unavailable");
    int efficiency = -1;
    for (size_t at = 0; at < bytes;) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data() + at);
        for (WORD group = 0; group < info->Processor.GroupCount; ++group) {
            const auto& mask = info->Processor.GroupMask[group];
            auto available = mask.Mask & process_mask;
            if (mask.Group == 0 && available && info->Processor.EfficiencyClass > efficiency) {
                efficiency = info->Processor.EfficiencyClass;
                selected = available & (~available + 1);
            }
        }
        at += info->Size;
    }
    if (!selected || !SetThreadAffinityMask(GetCurrentThread(), selected))
        throw std::runtime_error("Cannot pin measurement thread");
    int regs[4];
    __cpuidex(regs, 0x1a, 0);
    std::cout << "logical_cpu=" << std::countr_zero(selected) << " efficiency_class=" << efficiency
              << " intel_core_type=" << ((unsigned(regs[0]) >> 24) & 255) << '\n';
}

struct Data {
    int side, count;
    float step;
    std::vector<float> x, z, a, b, c, d, out, ax, bx, cz, dz;
    explicit Data(int size, float spacing)
        : side(size), count(size * size), step(spacing), x(count), z(count), a(count), b(count), c(count),
          d(count), out(count), ax(size), bx(size), cz(size), dz(size) {
        for (int row = 0; row < side; ++row)
            for (int col = 0; col < side; ++col) {
                x[row * side + col] = 251.375f + col * step;
                z[row * side + col] = 531.25f + row * step;
            }
        torus_grid();
    }
    // General scattered-position path. Scalar libm calls are deliberately reported separately.
    void torus_point() {
        for (int i = 0; i < count; ++i) {
            const float tx = x[i] * (tau / period), tz = z[i] * (tau / period);
            a[i] = std::cos(tx) * (period / tau);
            b[i] = std::sin(tx) * (period / tau);
            c[i] = std::cos(tz) * (period / tau);
            d[i] = std::sin(tz) * (period / tau);
        }
    }
    // Regular Cartesian grid: compute sin/cos once per axis coordinate, then fill SoA arrays.
    void torus_grid() {
        for (int i = 0; i < side; ++i) {
            const float tx = x[i] * (tau / period), tz = z[i * side] * (tau / period);
            ax[i] = std::cos(tx) * (period / tau);
            bx[i] = std::sin(tx) * (period / tau);
            cz[i] = std::cos(tz) * (period / tau);
            dz[i] = std::sin(tz) * (period / tau);
        }
        for (int row = 0; row < side; ++row)
            for (int col = 0; col < side; ++col) {
                const int i = row * side + col;
                a[i] = ax[col];
                b[i] = bx[col];
                c[i] = cz[row];
                d[i] = dz[row];
            }
    }
};

FastNoise::SmartNode<> fractal(FastNoise::SmartNode<> source, int octaves) {
    if (octaves == 1)
        return source;
    auto f = FastNoise::New<FastNoise::FractalFBm>(feature);
    f->SetSource(source);
    f->SetOctaveCount(octaves);
    f->SetGain(.5f);
    f->SetLacunarity(2);
    f->SetWeightedStrength(0);
    return f;
}
struct Nodes {
    FastNoise::SmartNode<> perlin, periodic, simplex, psrd;
    explicit Nodes(int octaves) {
        const float normalization = 1 / (2.0f * (1.0f - std::pow(.5f, float(octaves))));
        auto p = FastNoise::New<FastNoise::Perlin>(feature);
        p->SetScale(1024);
        p->SetOutputMin(-normalization);
        p->SetOutputMax(normalization);
        auto s = FastNoise::New<FastNoise::Simplex>(feature);
        s->SetScale(1024);
        s->SetOutputMin(-normalization);
        s->SetOutputMax(normalization);
        auto periodic_node = FastNoise::New<FastNoise::PeriodicPerlin>(feature);
        periodic_node->Configure({10, octaves, .5f, 0, {}}, 48);
        perlin = fractal(p, octaves);
        simplex = fractal(s, octaves);
        periodic = periodic_node;
        auto psrd_node = FastNoise::New<FastNoise::BenchmarkPsrd>(feature);
        psrd_node->Configure(octaves);
        psrd = psrd_node;
        if (perlin->GetActiveFeatureSet() != feature || simplex->GetActiveFeatureSet() != feature ||
            periodic->GetActiveFeatureSet() != feature || psrd->GetActiveFeatureSet() != feature)
            throw std::runtime_error("AVX2 was not selected for every node");
    }
    FastNoise::OutputMinMax call(Data& data, int kind, int seed) const {
        if (kind == 5)
            return psrd->GenPositionArray2D(data.out.data(), data.count, data.x.data(), data.z.data(), 0, 0,
                                            seed);
        if (kind < 2)
            return (kind == 0 ? perlin : periodic)
                ->GenPositionArray2D(data.out.data(), data.count, data.x.data(), data.z.data(), 0, 0, seed);
        if (kind == 3)
            data.torus_grid();
        if (kind == 4)
            data.torus_point();
        return simplex->GenPositionArray4D(data.out.data(), data.count, data.a.data(), data.b.data(),
                                           data.c.data(), data.d.data(), 0, 0, 0, 0, seed);
    }
};

double measure(const Nodes& nodes, Data& data, int kind, int iterations, int seed) {
    float checksum = 0;
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        nodes.call(data, kind, seed + (i & 1023));
        checksum += data.out[(i & 255) % data.count];
    }
    const auto stop = Clock::now();
    observable = checksum;
    if (!std::isfinite(checksum))
        throw std::runtime_error("Nonfinite measurement output");
    return std::chrono::duration<double, std::nano>(stop - start).count() / iterations / data.count;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool psrd_mode = argc == 3 && std::string(argv[1]) == "--psrd";
        if (argc != 2 && !psrd_mode)
            throw std::runtime_error("Usage: noise_benchmark output.csv | --psrd output_directory");
        const auto folder = psrd_mode ? std::filesystem::path(argv[2]) : std::filesystem::path{};
        if (psrd_mode)
            std::filesystem::create_directories(folder);
        const auto output = psrd_mode ? folder / "timings.csv" : std::filesystem::path(argv[1]);
        const std::vector<int> active = psrd_mode ? std::vector<int>{1, 5} : std::vector<int>{0, 1, 2, 3, 4};
        pin_cpu();
        std::cout << "AVX2; single thread; Release; 31 shuffled rounds; target 20ms per observation\n";
        std::ofstream csv(output, std::ios::trunc);
        if (!csv)
            throw std::runtime_error("Cannot write CSV");
        csv << "side,samples,step,octaves,method,round,iterations,ns_per_sample\n" << std::setprecision(12);
        if (psrd_mode) {
            for (int octaves : {1, 6}) {
                Nodes nodes(octaves);
                inspect_psrd(folder, nodes.periodic.get(), nodes.psrd.get(), octaves);
            }
        }
        std::mt19937 random(1337);
        for (int side : {5, 16, 256}) {
            Data data(side, side == 5 ? 4.0f : side == 16 ? 1.0f : 512.0f);
            for (int octaves : {1, 6}) {
                Nodes nodes(octaves);
                std::array<int, 6> iterations{};
                // Warm up each path and calibrate outside recorded observations.
                for (int kind : active) {
                    int n = 1;
                    double time;
                    do {
                        time = measure(nodes, data, kind, n, 1337);
                        if (time * n * data.count >= 4000000)
                            break;
                        n *= 2;
                    } while (n < 1048576);
                    iterations[kind] = std::clamp(int(20000000.0 / (time * data.count)), 1, 2000000);
                    measure(nodes, data, kind, iterations[kind], 1337);
                }
                auto order = active;
                for (int round = 0; round < 31; ++round) {
                    std::shuffle(order.begin(), order.end(), random);
                    for (int kind : order) {
                        double ns = measure(nodes, data, kind, iterations[kind], 1337 + round * 1031);
                        csv << side << ',' << data.count << ',' << data.step << ',' << octaves << ','
                            << names[kind] << ',' << round << ',' << iterations[kind] << ',' << ns << '\n';
                    }
                }
                if (!psrd_mode) {
                    nodes.call(data, 3, 1337);
                    auto reference = data.out;
                    nodes.call(data, 4, 1337);
                    float error = 0;
                    for (int i = 0; i < data.count; ++i)
                        error = std::max(error, std::abs(reference[i] - data.out[i]));
                    std::cout << "torus_path_max_error=" << error << ' ';
                }
                std::cout << "completed samples=" << data.count << " octaves=" << octaves << std::endl;
                csv.flush();
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
