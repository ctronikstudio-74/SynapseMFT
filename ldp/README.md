# LDP — Synapse Delivery Protocol

LDP is a secure file-transfer protocol. This folder is the protocol and the session engine. Gateway, virtual desktops, and the Synapse governance suite are separate commercial products.

LDP è un protocollo di trasferimento file. Questa cartella è il componente da adottare e da pubblicare come progetto open source a sé. Il resto della suite Synapse resta software commerciale.

## Cosa c’è qui

Il protocollo supportato si chiama **LDP-1**. Il codice è `LDP1Session.h`, `LDP1Session.cpp` e la cartella `pq/`. La libreria statica si chiama `ldp1`.

Due profili:

| Profilo | Scambio chiavi | Identità | Dati |
| --- | --- | --- | --- |
| `Classic` | X25519 | nessuna firma | XChaCha20-Poly1305 |
| `HybridPq1` | X25519 e ML-KEM-768 | ML-DSA-65, chiavi fissate prima dell’handshake | XChaCha20-Poly1305 |

ML-KEM-768 segue FIPS 203, ML-DSA-65 segue FIPS 204, tramite `mlkem-native` e `mldsa-native` ai commit in `pq/README.md`. Sono gli algoritmi di quegli standard. Questa build non è un modulo certificato CMVP e non è «validata FIPS 140».

Sul filo ogni datagramma inizia con 8 byte: magic `LDP1`, versione 1.0, flag (bit 0 = profilo ibrido), tipo. Poi nonce da 24 byte, campi in chiaro, ciphertext. Numeri interi in network byte order. C’è un solo autenticatore, quello dell’AEAD. HKDF è SHA-256, etichette `ldp1-client-tx` e `ldp1-server-tx`.

| Tipo | Nome | Ruolo |
| --- | --- | --- |
| 1 | ClientHello | chiave X25519; nel profilo ibrido anche la chiave di incapsulamento ML-KEM |
| 2 | ServerHello | chiave X25519; nel profilo ibrido ciphertext ML-KEM e firma del server |
| 3 | Data | chunk di file, finestra 8 |
| 4 | Ack | base cumulativa e bitmap da 64 bit |
| 5 | Rekey | nuova chiave da 32 byte, cifrata con la chiave precedente, attivata a un sequence concordato |
| 6 | Close | hash SHA-256 del file, in chiaro e dentro l’AEAD |
| 7 | ClientAuth | solo profilo ibrido: firma del client sull’intero ServerHello |
| 8 | RekeyAck | il sender usa la chiave nuova solo dopo questo messaggio |

Nel profilo ibrido le chiavi pubbliche non viaggiano nell’handshake. Si fissano con `set_identity` prima di `handshake`: chiave segreta del nodo locale, chiave pubblica del peer già nota. I contesti di firma sono `ldp1-server-hello`, `ldp1-client-auth` e `ldp1-close`. La sessione parte dopo la verifica delle due firme. L’hash di transcript è SHA-256 di ClientHello, ServerHello e ClientAuth. Il Close ibrido firma `hash del file || hash di transcript`.

Il trasporto è un’interfaccia. Chi adotta il componente implementa `Ldp1Transport::send` e `recv`. Il test usa una coda in memoria e scarta alcuni datagrammi Data per esercitare ritrasmissione e rekey. Un socket UDP non è ancora nel motore.

## Come si usa

```cpp
class UdpTransport : public Ldp1Transport {
public:
    void send(std::vector<uint8_t> datagram) override;
    bool recv(std::vector<uint8_t>& datagram, int timeout_ms) override;
};

UdpTransport transport;
Ldp1Session session(Ldp1Session::Role::Client, Ldp1Session::Profile::HybridPq1, transport);
session.set_identity(local_mldsa_sk, peer_mldsa_pk);
session.handshake();
session.send_all(file_bytes, 16 * 1024, 1000);
```

Il lato server usa `Role::Server`, la propria chiave segreta e la chiave pubblica del client, poi `receive_all`. `rekey_every` è il numero di chunk dopo il quale il sender propone una chiave nuova. Il valore `0` non fa rekey.

Un esempio completo di due sessioni, perdita e identità è `tests/test_ldp1.cpp`.

## Build

Dipendenze, nella cartella `vendor/` del repository:

- libsodium 1.0.20, DLL MSVC x64 (non definire `SODIUM_STATIC`)
- `mlkem-native` commit `3979073e2137f59640560673de88c9949a2105ef`
- `mldsa-native` commit `6797c557aabb3574915f2d6bfbb7b973ae988525`

I dettagli sono in `pq/README.md`. CMake le legge da `vendor/SynapseCrypto.cmake`.

Su Windows, da questa cartella:

```bat
build_ldp1.bat
```

Lo script trova Visual Studio con `vswhere`, inizializza il prompt x64 e compila con NMake. In alternativa, da un prompt già inizializzato con `vcvars64.bat`:

```bat
cmake -G "NMake Makefiles" -S tests -B build
cmake --build build --target ldp1 ldp1_tests
build\ldp1_tests.exe
```

`ldp1` è la libreria statica da collegare. `ldp1_tests.exe` va eseguito accanto a `libsodium.dll`, copiata in build dal target. L’esito atteso è `ldp1 hybrid pq transfer passed` e `ldp1 session tests passed`.

`build_windows.bat`, `build_linux.sh` e `build_macos.sh` compilano la bozza precedente (`CMakeLists.txt` in radice, target `ldp`). Quella bozza non è il protocollo da adottare.

La prova da dieci minuti e il benchmark pubblico stanno nel progetto `ldp-sdk`, accanto a questa cartella.

## Layout

| Percorso | Ruolo |
| --- | --- |
| `LDP1Session.h`, `LDP1Session.cpp` | sessione LDP-1 |
| `pq/` | ML-KEM-768, ML-DSA-65, commit delle librerie |
| `tests/test_ldp1.cpp` | handshake, perdita, rekey, identità, firma alterata |
| `tests/test_packet_and_congestion.cpp` | serializzazione e congestione della bozza precedente |
| `CMakeLists.txt` e gli altri `.cpp` in radice | bozza precedente del trasmettitore |
| `ldp_api.h`, `ldp_core.i`, `main.cpp`, `test_ldp.cpp` | schizzo di API e di programma, non collegati a LDP-1 |

La bozza precedente resta nel tree come storia del lavoro. Ha un altro formato di pacchetto, non va mescolata a LDP-1, e non è l’interfaccia pubblica del componente.

## Open source e parti a pagamento

Questo repository, quando viene pubblicato, è il protocollo e la libreria `ldp1`. L’SDK in `ldp-sdk` è lo stesso perimetro gratuito.

Tre strumenti non sono in questo codice e si comprano con licenza annuale, fattura, dodici mesi anticipati, prezzi IVA esclusa:

| Strumento | Cosa fa | Prezzo pubblico |
| --- | --- | --- |
| SynapseMFT Governance Suite | piano di controllo: approvazione, audit, policy sui nodi | Starter €6.000/anno fino a 10 nodi. Professional €18.000/anno fino a 50 nodi. Enterprise da €36.000/anno su preventivo |
| Virtual Desktop | console per un operatore nominato | €1.200 a posto all’anno. Dieci posti sono dentro Professional. Si vende solo insieme alla Governance |
| Synapse Event Bus | bus cifrato degli eventi di trasferimento e di governance | Dentro Professional ed Enterprise. Da solo, o in aggiunta a Starter: €4.800/anno fino a 10 nodi, €9.600/anno fino a 50 |

La licenza perpetua esiste solo per la Governance Suite: tre volte il canone annuo, poi manutenzione al 22% dal secondo anno. L’ordine parte dal portale C-Tronik, pagina SynapseMFT. Il sito di prodotto descrive le tre voci e rimanda a quell’ordine. Protocollo e SDK non sono una riga di fattura.

Chi adotta LDP implementa da solo trasporto e chiavi. Collegare Governance, desktop o event bus è un contratto commerciale, non una clausola di questo componente.

## Licenza

La licenza open source non è ancora nel tree. Va scelta e aggiunta, in un file `LICENSE`, prima del primo tag pubblico. Fino a quel file il codice non è una concessione d’uso. La forma adatta a un componente di protocollo è una licenza permissiva; Apache-2.0 è la candidata, per la concessione di brevetto, e non è ancora applicata.

Le librerie in `vendor/` restano sotto le loro licenze. Non vanno vendute come parte del certificato di questo componente.

## Stato attuale

LDP-1 fa handshake, trasferimento, SACK, ritrasmissione e rekey negoziato. Il profilo ibrido rifiuta una chiave pubblica sbagliata e una firma alterata. Il test che lo dimostra è passato su Windows.

Per un trasferimento su rete mancano ancora il socket UDP dentro il motore, il controllo di congestione collegato alla sessione, il messaggio di kill e il padding a lunghezza costante. Il profilo classico non firma il transcript.
