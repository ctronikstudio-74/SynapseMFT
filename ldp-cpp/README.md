# LDP Agent Server & API - C++

Implementazione C++ del sistema ad agenti LDP con API REST per gestire nodi.

## Struttura

- `LDPAgentServer` - Server agente che ascolta comandi TCP su ogni nodo
- `LDPAgentManager` - Gestisce istanziazione e comunicazione con agenti
- `LDPApiServer` - Server HTTP REST per esporre API a client esterni

## Compilazione

### Requisiti
- CMake 3.15+
- Compilatore C++17 (GCC, Clang, MSVC)
- OpenSSL (opzionale, per crittografia avanzata)

### Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Eseguibile

L'eseguibile `ldp-api-server` sarà in `build/` (o `build/Debug/` su Windows).

## Avvio

```bash
./ldp-api-server [porta]
```

Default: porta 3002

## API Endpoints

- `POST /api/agents/start` - Avvia agente
  ```json
  {"nodeId": "1", "nodeIp": "127.0.0.1"}
  ```

- `POST /api/agents/stop` - Ferma agente
  ```json
  {"nodeId": "1"}
  ```

- `GET /api/agents` - Lista agenti attivi

- `POST /api/agent/:nodeId` - Invia comando a agente
  ```json
  {"ip": "127.0.0.1", "command": "{\"type\":\"PING\"}"}
  ```

## Comandi Agente

- `PING` - Verifica stato
- `LIST_FILES` - Lista file in cartella
- `CREATE_FOLDER` - Crea cartella
- `UPLOAD_FILE` - Carica file
- `DOWNLOAD_FILE` - Scarica file
- `TRANSFER_FILE` - Trasferisce file tra nodi
- `DECRYPT_FILE` - Decripta file
- `GET_STATUS` - Ottiene stato nodo

## Note

- Gli agenti usano porte incrementali: `8888 + nodeId`
- File system virtuale in `nodes/node-<id>/`
- Supporta modalità demo (tutti nodi su stessa macchina)

