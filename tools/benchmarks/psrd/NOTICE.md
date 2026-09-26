# PSRD benchmark provenance

AI context. Source: Stefan Gustavson and Ian McEwan, psrdnoise2.glsl (2021-12-02), MIT.

- https://github.com/stegu/psrdnoise
- https://raw.githubusercontent.com/stegu/psrdnoise/main/src/psrdnoise2.glsl
- Vendored source SHA256: 3abb8a203480c79363677995f38c8ffeb1fd9d1eb941473a6f3c4a07532458a6
- Full license is retained in the original and SIMD adaptation psrd.inl. This benchmark code is not packaged with the game.

The C++ port fixes alpha=0, omits unused derivatives, and uses FastSIMD AVX2 Sin/Cos approximations with RELAXED /fp:fast. Hash arithmetic and corner wrapping follow the original. The original has no seed; this port adds ((seed ^ (seed >> 16)) & 65535) modulo289 to the first hash input. Seed0 for one octave matches the original static field. Other seeds and seed+octave are a benchmark extension, with only289 initial salt states, not independent full32-bit hash realizations. The double reference uses original float constants and accurate trigonometry.

Period X/Z131072, base spacing1024, period in noise coordinates128. Coordinates and lattice period double per octave. Axis periods are valid integer X/even Y inputs (Y maps to world Z). This is only a 2D node; the required 3D virtual method throws explicitly. It is not a game-ready general noise API. The polynomial hash modulo289 has intrinsic repeated gradient structure in large lattice regions; power-of-two external corner wrapping does not eliminate local repetitions. Further seed/hash changes would produce a different variant needing its own comparison.

summarize.py requires numpy and matplotlib only for offline report plots. They were installed under build/benchmark-python, without changing global Python or runtime dependencies. Raw image arrays stay in the explicitly supplied run directory; PNG/CSV/diagnostic summaries are retained under docs/benchmarks.
