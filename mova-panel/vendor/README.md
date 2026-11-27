# MOVA Panel dependencies

Only jsmn v1.1.0 (MIT) and the public Mozilla CA bundle are kept in this folder.
The DejaVu font and its license are in ../assets/.

Mbed TLS 2.28.10 and zlib 1.3.1 are fetched by ../build_deps.py using the release
URLs and SHA-256 digests in ../dependencies.lock.json. Verified archives and
extracted sources stay in ignored ../build/dependencies/. Their full license
notices are copied to the SD package by build.py. No separate source archive is
created or included in the repository. No dependency download runs on the display.

Upstreams:

- https://github.com/Mbed-TLS/mbedtls/tree/v2.28.10
  Apache-2.0 OR GPL-2.0-or-later, dual licensed.
- https://github.com/madler/zlib/tree/v1.3.1 (zlib license).
- https://github.com/zserge/jsmn/tree/v1.1.0 (MIT).
- https://curl.se/docs/caextract.html (public CA roots; extraction date in PEM header).

Cloud protocol reference: exclusively Tasshack/dreame-vacuum; see
../PROTOCOL-SOURCE.txt. This client uses the robot's last manual cleaning settings.
