# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Pica Pica: a distributed, end-to-end encrypted messenger. `pica-node` (C, libevent, SQLite) relays traffic; `pica-client` (C++/Qt5) is the GUI client. GPL-3.0-only.

## Build

Autotools, from the repo root:

```sh
autoreconf --install          # only for a fresh checkout without ./configure
./configure                   # --disable-node / --disable-client, --with-qt=qt4|qt5 (qt5 default; Qt4 is no longer really buildable)
make -j$(nproc)               # produces ./pica-node and ./pica-client/pica-client
```

- New source files must be added to `Makefile.am` (and its `moc_*.cpp` list for QObject classes) and to `pica-client/pica-client.pro` (alternative qmake build).
- Optional media features are autodetected; `debian/rules` forces `--enable-vaapi --enable-vaapi-x11 --enable-pulse`. Windows: `--enable-d3d12va`.
- Packaging: `deb_packaging/build-client-deb.sh` builds the client .deb for this host; `deb_packaging/build-pica-packages.sh` builds all distros in podman and assembles a signed apt repo (see `deb_packaging/README.md`). Windows cross-build: `windows/cross_build_mxe.sh` (MXE).

## Tests

There is no `make check`. `tests/*.c` are manual programs that link against the client library objects and talk to a live node, e.g. `tests/test_client.c` (needs `-a node_addr -p port -c cert`). To test networking locally, run a node with a scratch config:

```ini
[pica-node]
announced_addr = 127.0.0.1
listen_port = 22330
nodes_db_file = /path/to/empty-nodes.db   # sqlite3 file.db < nodelist_db_create.sql; delete from nodes;
dh_param_file = /path/to/repo/dhparam4096.pem
disable_reserved_addrs = no                # otherwise 127.0.0.1 is rejected
```

`./pica-node -f node.conf -v`. Test accounts need **4096-bit RSA** self-signed certs (`PICA_RSA_MINKEYSIZE`), e.g. `openssl req -x509 -newkey rsa:4096 -nodes ...`; smaller keys fail the c2c TLS step silently. A `PICA_client_callbacks` struct must have every callback the library calls unconditionally set (e.g. `nodelist_cb`), or it segfaults. Link C test programs against the built `pica_client_pica_client-PICA_*.o` objects (compiling `PICA_client.c` as C++ fails).

The client can be smoke-tested headless with a throwaway home: `HOME=/tmp/x QT_QPA_PLATFORM=offscreen pica-client/pica-client`.

## MCP servers

- **context7** — use before writing or changing code that calls a third-party API, instead of relying on memory. This code mixes old and new API generations (Qt 5 alongside Qt 4 leftovers, OpenSSL 1.0 `#if` branches next to 3.x), so check the version you need. Library IDs:
  - Qt 5: `/qt/qtbase` (version `__branch__5.14`) or `/websites/doc_qt_io` — QDataStream, QSqlDatabase per-thread rules, QMetaObject::invokeMethod, signals/slots.
  - OpenSSL: `/openssl/openssl` (3.x) — EVP signing/verification, X509, SSL/DTLS contexts, deprecated RSA_* APIs.
  - libevent: `/websites/libevent_doc` — pica-node's event loop.
  - FFmpeg: `/websites/ffmpeg_doxygen` (versions 4.4/7.0/8.0) — `videodevice.cpp`, VAAPI/D3D12VA hardware acceleration.
- **codebase-memory-mcp** — structural questions about this repo: who calls a function, which callbacks reach a code path, the impact of changing a `PICA_*` function or a `struct PICA_c2c` field. The repo is not indexed yet: run `index_repository` on the repo root first. Use grep instead for protocol constants and literals (`PICA_PROTO_*`, SQL strings), and for `SIGNAL(...)`/`SLOT(...)` string connections, which a call graph does not see.
- **deepwiki** — questions about upstream GitHub projects the build depends on, e.g. `mxe/mxe` for `windows/cross_build_mxe.sh`, or how a dependency implements something its docs don't cover.
- **parallel-search** — current, version-specific facts that change over time: Debian/Ubuntu package names and versions for `debian/control` or the build matrix in `deb_packaging/`, OpenSSL/Qt deprecations, driver or codec support. Use `web_fetch` for a specific page such as a Debian changelog or an MXE package file.

## Architecture

**Shared C code at the root** (`PICA_*.c/h`), compiled into both programs:
- `PICA_proto.h` — wire protocol: message type codes and fixed sizes. Every message is `[type type]` + payload; variable-size ones carry a 2-octet length. `doc/proto-doc-latest.txt` documents it; keep it in sync.
- `PICA_msgproc.c` — framing/dispatch from `PICA_msginfo` tables. **An unknown message type makes the receiver drop the connection**, so new c2c messages must only be sent to peers known to support them (see the EXTMSG negotiation: `PICA_PROTO_INITRESPOK_EXTMARK` / `PICA_PROTO_EXTHELLO`, `peer_ext` on `struct PICA_c2c`).
- `PICA_client.c` — the client network library: c2n (client→node), c2c (client↔client, relayed by the node, end-to-end TLS with each account's certificate), directc2c (direct TCP), and `PICA_media.c` mediac2c (DTLS/UDP for call media). Single-threaded, driven by `PICA_event_loop()`; reports everything through `struct PICA_client_callbacks` (callbacks are positional, so append new ones at the end).
- An account ID is the SHA-224 of the DER certificate (`PICA_id.c`).

**pica-node** (`PICA_node.c`, libevent): authenticates clients, keeps the distributed node list (`nodelist.db`), finds users across nodes, and relays c2c connections. It never sees message contents or stores messages, and it deliberately refuses c2c connections to your own ID.

**pica-client** (`pica-client/`):
- Threads: the GUI thread; the network thread, where `SkyNet` (`skynet.cpp`, global `skynet`) runs the event loop on a 100 ms timer; plus audio/video threads. Public `SkyNet` methods forward to `do_*` slots via `QMetaObject::invokeMethod`. Each thread uses its own `QSqlDatabase` connection (the network one is named `network_thread_connection`), passed as `connectionName` to the DB wrapper classes.
- Storage: a single SQLite DB `~/.pica-client/pica-client.sqlite`. Schema creation and versioned upgrades are in `main.cpp` (`create_database()` / `update_database()`, table `schema_version`). Add a new `if (schema_ver == N)` step; never change old steps.
- Thin DB wrappers: `Accounts`, `Contacts`, `Nodes`, `Settings`, `History` + `Groups` (`history.cpp`).
- Messaging: `SkyNet` stores every sent and received message itself. Outgoing messages go through the `outbox` table (one row per recipient, resent on reconnect). Messages are signed with the account key and hash-chained (`History::SignedData` / `Hash`). Groups use sender fan-out with owner-signed group info. When two new clients connect they sync missing history from each other (`EXT_*` handlers in `skynet.cpp`). Peers on older clients get plain `MSGUTF8` messages, acknowledged in order.
- UI routing: `MsgUIRouter` (global `msguirouter`) connects `SkyNet` signals to `ChatWindow`s, keyed by conversation ID (a peer ID, or a 16-byte group ID). `ContactListWidget` lists contacts and groups.
- Calls: `AudioVideoCallController`, with `audiodevice`/`nativeaudio_*` (ALSA/PulseAudio/WASAPI/CoreAudio, WebRTC AEC3), `videodevice` (FFmpeg) and `vaapi*` hardware video.
