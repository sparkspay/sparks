Sparks Core
==========

This is the official reference wallet for Sparks digital currency and comprises the backbone of the Sparks peer-to-peer network. You can [download Sparks Core](https://www.sparkspay.io/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run Sparks on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/sparks-qt` (GUI) or
- `bin/sparksd` (headless)

### Windows

Unpack the files into a directory, and then run sparks-qt.exe.

### macOS

Drag Sparks-Qt to your applications folder, and then run Sparks-Qt.

### Need Help?

* See the [Sparks documentation](https://sparkspay.atlassian.net/wiki/display/DOC)
for help and more information.
* Ask for help on [Sparks Nation Discord](http://sparkschat.org)
* Ask for help on the [Sparks Forum](https://sparkspay.io/forum)

Building
---------------------
The following are developer notes on how to build Sparks Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The Sparks Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- Source Code Documentation ***TODO***
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Travis CI](travis-ci.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* Discuss on the [Sparks Forum](https://sparkspay.io/forum), in the Development & Technical Discussion board.
* Discuss on [Sparks Nation Discord](http://sparkschat.org)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [sparks.conf Configuration File](sparks-conf.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Memory](reduce-memory.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)
- [PSBT support](psbt.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
