Dependencies
============

These are the dependencies currently used by Sparks Core. You can find instructions for installing them in the `build-*.md` file for your platform.

| Dependency | Version used | Minimum required | CVEs | Shared | [Bundled Qt library](https://doc.qt.io/qt-5/configure-options.html#third-party-libraries) |
| --- | --- | --- | --- | --- | --- |
| Berkeley DB | [4.8.30](https://www.oracle.com/technetwork/database/database-technologies/berkeleydb/downloads/index.html) | 4.8.x | No |  |  |
| Boost | [1.73.0](https://www.boost.org/users/download/) | [1.64.0](https://github.com/bitcoin/bitcoin/pull/22320) | No |  |  |
| Clang<sup>[ \* ](#note1)</sup> |  | [5.0+](https://releases.llvm.org/download.html) (C++17 support) |  |  |  |
| D-Bus | [1.10.18](https://cgit.freedesktop.org/dbus/dbus/tree/NEWS?h=dbus-1.10) |  | No | Yes |  |
| Expat | [2.2.7](https://libexpat.github.io/) |  | No | Yes |  |
| fontconfig | [2.12.1](https://www.freedesktop.org/software/fontconfig/release/) |  | No | Yes |  |
| FreeType | [2.7.1](https://download.savannah.gnu.org/releases/freetype) |  | No |  | [Yes](https://github.com/sparkspay/sparks/blob/develop/depends/packages/qt.mk) (Android only) |
| GCC |  | [7+](https://gcc.gnu.org/) (C++17 support) |  |  |  |
| glibc | | [2.18](https://www.gnu.org/software/libc/) |  |  |  |  |
| HarfBuzz-NG |  |  |  |  | [Yes](https://github.com/sparkspay/sparks/blob/develop/depends/packages/qt.mk) |
| libevent | [2.1.11-stable](https://github.com/libevent/libevent/releases) | [2.0.21](https://github.com/bitcoin/bitcoin/pull/18676) | No |  |  |
| libnatpmp | git commit [4536032...](https://github.com/miniupnp/libnatpmp/tree/4536032ae32268a45c073a4d5e91bbab4534773a) |  | No |  |  |
| libpng |  |  |  |  | [Yes](https://github.com/sparkspay/sparks/blob/develop/depends/packages/qt.mk) |
| librsvg | |  |  |  |  |
| MiniUPnPc | [2.2.2](https://miniupnp.tuxfamily.org/files) |  | No |  |  |
| PCRE |  |  |  |  | [Yes](https://github.com/sparkspay/sparks/blob/develop/depends/packages/qt.mk) |
| Python (tests) |  | [3.5](https://www.python.org/downloads) |  |  |  |
| qrencode | [3.4.4](https://fukuchi.org/works/qrencode) |  | No |  |  |
| Qt | [5.12.11](https://download.qt.io/official_releases/qt/) | [5.5.1](https://github.com/bitcoin/bitcoin/issues/13478) | No |  |  |
| SQLite | [3.32.1](https://sqlite.org/download.html) | [3.7.17](https://github.com/bitcoin/bitcoin/pull/19077) |  |  |  |
| XCB |  |  |  |  | [Yes](https://github.com/sparkspay/sparks/blob/develop/depends/packages/qt.mk) (Linux only) |
| xkbcommon |  |  |  |  | [Yes](https://github.com/sparkspay/sparks/blob/develop/depends/packages/qt.mk) (Linux only) |
| ZeroMQ | [4.3.1](https://github.com/zeromq/libzmq/releases) | 4.0.0 | No |  |  |
| zlib |  |  |  |  | [Yes](https://github.com/sparkspay/sparks/blob/develop/depends/packages/qt.mk) |

<a name="note1">Note \*</a> : When compiling with `-stdlib=libc++`, the minimum supported libc++ version is 7.0.

Controlling dependencies
------------------------
Some dependencies are not needed in all configurations. The following are some factors that affect the dependency list.

#### Options passed to `./configure`
* MiniUPnPc is not needed with `--without-miniupnpc`.
* Berkeley DB is not needed with `--disable-wallet` or `--without-bdb`.
* SQLite is not needed with `--disable-wallet` or `--without-sqlite`.
* libnatpmp is not needed with `--without-natpmp`.
* Qt is not needed with `--without-gui`.
* If the qrencode dependency is absent, QR support won't be added. To force an error when that happens, pass `--with-qrencode`.
* ZeroMQ is needed only with the `--with-zmq` option.

#### Other
* librsvg is only needed if you need to run `make deploy` on (cross-compilation to) macOS.
* Not-Qt-bundled zlib is required to build the [DMG tool](../contrib/macdeploy/README.md#deterministic-macos-dmg-notes) from the libdmg-hfsplus project.
