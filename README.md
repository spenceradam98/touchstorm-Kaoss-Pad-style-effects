# Touchstorm (working name)

An XY touch-pad effects processor and loop sampler (VST3 + standalone): 50 effect programs in
8 categories, Hold, Pad Motion, tap/host tempo, 4 loop banks, 8 memories, MIDI control and
5 UI themes. See `docs/DESIGN.md`.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release            # fetches JUCE 8.0.4
# or: cmake -B build -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
ctest --test-dir build                                # engine tests
```

macOS builds target macOS 11+ universal (Intel + Apple Silicon). No AVX2 or other `-march`
flags are used; the DSP is scalar C++. Windows builds use the static runtime.

Layout: `Source/engine` is plain C++ (programs, pad logic, motion, loop banks) with tests in
`tests/`. `Source/plugin` is the JUCE wrapper and UI.
