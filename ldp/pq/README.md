# ML-KEM-768 e ML-DSA-65

Il profilo `HybridPq1` di LDP-1 usa due librerie del PQ Code Package:

| Algoritmo | Standard | Libreria | Commit |
| --- | --- | --- | --- |
| ML-KEM-768 | FIPS 203 | [mlkem-native](https://github.com/pq-code-package/mlkem-native) | `3979073e2137f59640560673de88c9949a2105ef` |
| ML-DSA-65 | FIPS 204 | [mldsa-native](https://github.com/pq-code-package/mldsa-native) | `6797c557aabb3574915f2d6bfbb7b973ae988525` |

Le librerie stanno in `third_party/`, ignorato da git. I comandi sotto vanno lanciati dalla cartella `ldp` quando `ldp` è il repository. Nel tree SynapseMFT le stesse librerie stanno in `third_party/` del repository padre. CMake cerca prima `ldp/third_party`, poi la cartella padre.

```powershell
git clone https://github.com/pq-code-package/mlkem-native.git third_party/mlkem-native
git -C third_party/mlkem-native checkout 3979073e2137f59640560673de88c9949a2105ef
git clone https://github.com/pq-code-package/mldsa-native.git third_party/mldsa-native
git -C third_party/mldsa-native checkout 6797c557aabb3574915f2d6bfbb7b973ae988525
```

Il codice implementa gli algoritmi dei due FIPS. Non è un modulo certificato CMVP e non va presentato come “FIPS 140 validato”. La casualità è `randombytes_buf` di libsodium.

Il codice di riferimento Kyber768 non è più collegato: un ciphertext Kyber non è un ciphertext ML-KEM.

## Build del test

`ldp1_tests` si compila se sono presenti libsodium 1.0.20 (`third_party/libsodium-msvc`, DLL dinamica, senza `SODIUM_STATIC`), `mlkem-native` e `mldsa-native` ai commit sopra. La build verificata usa NMake, da un prompt x64 già inizializzato con `vcvars64.bat`:

```bat
cmake -G "NMake Makefiles" -S tests -B build
cmake --build build --target ldp1 ldp1_tests
build\ldp1_tests.exe
```

I comandi partono dalla cartella `ldp`. Il target `ldp1` è la libreria statica del protocollo. `ldp1_tests` è il test di sessione.

L’eseguibile stampa `ldp1 hybrid pq transfer passed` e `ldp1 session tests passed`. Il trasporto del test è una coda in memoria, non UDP.
