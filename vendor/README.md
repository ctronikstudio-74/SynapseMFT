# Librerie crittografiche del progetto

Copie usate da `Ldp1Session`, dall'SDK e da `HybridIdentity`. Non sono un modulo CMVP.

| Cartella | Origine | Ruolo |
| --- | --- | --- |
| `mlkem/` | pq-code-package mlkem-native `3979073e2137f59640560673de88c9949a2105ef` | ML-KEM-768, FIPS 203. È l'albero di compilazione, senza test e senza Nix |
| `mldsa/` | pq-code-package mldsa-native `6797c557aabb3574915f2d6bfbb7b973ae988525` | ML-DSA-65, FIPS 204. Stesso taglio |
| `libsodium/` | libsodium 1.0.20, binario MSVC x64 Release già usato dal progetto | X25519, HKDF-SHA256, XChaCha20-Poly1305. Licenza ISC |

`SynapseCrypto.cmake` è il punto unico letto da `ldp`, `ldp-sdk` e `ldp-cpp`. Se questa copia manca, lo script cerca ancora un checkout `third_party/`. `kyber` non è stato copiato: nessun target lo collega.

Il caso casuale di ML-KEM e ML-DSA passa da libsodium, tramite `ldp/pq/ldp_mlkem_config.h` e `ldp/pq/ldp_mldsa_config.h`.
