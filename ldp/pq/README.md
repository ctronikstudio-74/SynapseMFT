# ML-KEM-768 e ML-DSA-65

Il profilo `HybridPq1` di LDP-1 usa due librerie del PQ Code Package:

| Algoritmo | Standard | Libreria | Commit |
| --- | --- | --- | --- |
| ML-KEM-768 | FIPS 203 | [mlkem-native](https://github.com/pq-code-package/mlkem-native) | `3979073e2137f59640560673de88c9949a2105ef` |
| ML-DSA-65 | FIPS 204 | [mldsa-native](https://github.com/pq-code-package/mldsa-native) | `6797c557aabb3574915f2d6bfbb7b973ae988525` |

Nel repository SynapseMFT le copie usate dalla build stanno in `vendor/`: `mlkem/`, `mldsa/` e `libsodium/`. I commit sopra sono quelli di origine. Non serve un clone separato.

Il codice implementa gli algoritmi dei due FIPS. Non è un modulo certificato CMVP e non va presentato come “FIPS 140 validato”. La casualità è `randombytes_buf` di libsodium.

Il codice di riferimento Kyber768 non è più collegato: un ciphertext Kyber non è un ciphertext ML-KEM.

## Build del test

`ldp1_tests` si compila con le copie in `vendor/`: libsodium 1.0.20 come DLL (senza `SODIUM_STATIC`), ML-KEM-768 e ML-DSA-65 ai commit sopra. La build verificata usa NMake, da un prompt x64 già inizializzato con `vcvars64.bat`:

```bat
cmake -G "NMake Makefiles" -S tests -B build
cmake --build build --target ldp1 ldp1_tests
build\ldp1_tests.exe
```

I comandi partono dalla cartella `ldp`. Il target `ldp1` è la libreria statica del protocollo. `ldp1_tests` è il test di sessione.

L’eseguibile stampa `ldp1 hybrid pq transfer passed` e `ldp1 session tests passed`. Il trasporto del test è una coda in memoria, non UDP.
