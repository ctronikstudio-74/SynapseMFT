# Build

Le istruzioni del componente sono in [README.md](README.md), sezione Build. I dettagli di ML-KEM-768 e ML-DSA-65 sono in [pq/README.md](pq/README.md).

Da questa cartella: `build_ldp1.bat`.

Oppure, da un prompt x64 di Visual Studio:

```bat
cmake -G "NMake Makefiles" -S tests -B build
cmake --build build --target ldp1 ldp1_tests
build\ldp1_tests.exe
```

`build_windows.bat`, `build_linux.sh` e `build_macos.sh` configurano il `CMakeLists.txt` della bozza precedente. Quel target si chiama `ldp` e non è LDP-1.
