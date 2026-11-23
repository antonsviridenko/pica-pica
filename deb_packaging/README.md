# Debian and Ubuntu packaging

Builds the pica-pica packages for every distribution we support, each in its
own podman container, and assembles the results into a signed apt repository.

## Scripts

| | |
|---|---|
| `build-pica-packages.sh` | The one to run. Installs podman, builds every target, assembles and signs the repository. |
| `build-webrtc-audio-processing.sh` | Backports Debian's webrtc-audio-processing 1.3 to a distribution that has no 1.x of its own. Usable on its own; the orchestrator calls it per container. |
| `container-build.sh` | The in-container worker. Not run by hand. |

## Usage

Make a source tarball the usual way, then hand it over:

```sh
cd /path/to/pica-pica && ./configure && make dist
cd deb_packaging
./build-pica-packages.sh --gpg-key packaging@picapica.im \
    ../pica-pica-0.9.0dev.tar.gz
```

That leaves a repository in `repo/` and the raw `.deb` files in `out/`.

Useful while iterating:

```sh
./build-pica-packages.sh --only 'noble/*' --no-sign ../pica-pica-*.tar.gz
./build-pica-packages.sh --skip-build -k KEYID       # re-sign what is in out/
```

A `make dist` tarball is self-contained: it already carries the generated
`configure` and `Makefile.in`, so the containers have nothing to bootstrap.
They will fall back to `autoreconf -i -f` if handed a tarball made some other
way, and `autoconf`/`automake` are installed regardless — if the tarball's
timestamps leave `Makefile.am` looking newer than `Makefile.in`, automake's
own rebuild rules fire during the build and want them.

Whatever the tarball is called and however it is compressed, it is bind
mounted into each container under one fixed name; `tar` works the compression
out from the content.

## Build matrix

| Distribution | Codename | amd64 | i386 | webrtc-audio-processing |
|---|---|---|---|---|
| Ubuntu 24.04 LTS | noble | yes | **no** | backported by us (archive has 0.3.1) |
| Ubuntu 26.04 LTS | resolute | yes | **no** | from the archive (1.3-3build2) |
| Debian 13 | trixie | yes | yes | from the archive (1.3-3) |

### Why there is no Ubuntu i386

Ubuntu dropped i386 as an architecture after 18.04. What is left is a small
multiarch compatibility subset with no installable base system: there is no
i386 Ubuntu container image (`docker.io/i386/ubuntu` stops at `18.04`) and no
i386 Qt to build against. Debian still carries i386 as a full architecture,
including `qtbase5-dev`, so the 32 bit build is done there.

## The webrtc-audio-processing backport

pica-client needs AEC3, which arrived in webrtc-audio-processing 1.x. Ubuntu
24.04 ships 0.3.1, which predates it and has an entirely different API, so
noble needs a backport of Debian's 1.3-3. Nothing else does — the check is
made inside each container, and the script exits without doing anything where
the archive is already good enough.

Debian's packaging is taken unmodified except for two things:

1. **The `-dev` package is renamed** to `libwebrtc-audio-processing-1-dev`,
   which is the name Debian itself used for 1.3-1. Debian renamed it to the
   unversioned `libwebrtc-audio-processing-dev` in 1.3-2, after dropping 0.3
   from the archive — but noble still ships 0.3.1 under exactly that name, and
   taking it over would replace a package other things build against.

   Nothing else collides. Upstream already versions everything that matters:

   | | 0.3.1 (noble) | 1.3 (ours) |
   |---|---|---|
   | headers | `/usr/include/webrtc_audio_processing/` | `/usr/include/webrtc-audio-processing-1/` |
   | library | `libwebrtc_audio_processing.so.1` | `libwebrtc-audio-processing-1.so.3` |
   | pkg-config | `webrtc-audio-processing` | `webrtc-audio-processing-1` |
   | runtime deb | `libwebrtc-audio-processing1` | `libwebrtc-audio-processing-1-3` |

   So the four packages we produce — `libwebrtc-audio-processing-1-dev`,
   `libwebrtc-audio-processing-1-3`, `libwebrtc-audio-coding-dev`,
   `libwebrtc-audio-coding-1-3` — are all collision-free on noble.

2. **Debian's rename-era `Breaks`/`Replaces` are dropped.** With the old name
   back they say nothing, and the pair on `libwebrtc-audio-coding-dev` would
   otherwise declare `Replaces: libwebrtc-audio-processing-dev (<< 1.3-2~)`,
   which on noble matches the distribution's own 0.3 package — the exact thing
   this is trying not to disturb. The unrelated relationship between the two
   runtime packages is kept.

The version is `1.3-3~noble1`. The `~` sorts it *below* plain `1.3-3`, so if
Ubuntu ever ships 1.3 itself, apt upgrades away from the backport rather than
being held back by it.

## Signing

The GPG key never enters a container. Containers produce `.deb` files;
the repository is assembled and signed on the host. Both `InRelease`
(what modern apt fetches) and `Release.gpg` are produced, and the public key
is exported to `repo/pica-pica-archive-keyring.asc`.

Packages themselves are not signed with `debsigs` — apt does not check that;
repository trust comes from the signed `Release`.

## Repository layout

One pool per codename rather than a shared one. Indices are per suite, and a
shared pool would let noble's `Packages` list trixie's binaries — apt would
offer them and they would not install.

```
repo/
├── dists/<codename>/
│   ├── Release, Release.gpg, InRelease
│   └── main/binary-<arch>/{Packages,Packages.gz,Release}
├── pool/<codename>/main/<letter>/<source>/*.deb
└── pica-pica-archive-keyring.asc
```

## Client setup

```sh
sudo install -d /usr/share/keyrings
sudo curl -fsSL https://YOUR.HOST/pica-pica-archive-keyring.asc \
    -o /usr/share/keyrings/pica-pica-archive-keyring.asc

echo "deb [signed-by=/usr/share/keyrings/pica-pica-archive-keyring.asc] \
https://YOUR.HOST $(lsb_release -cs) main" \
    | sudo tee /etc/apt/sources.list.d/pica-pica.list

sudo apt update && sudo apt install pica-client
```
