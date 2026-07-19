# Vendored device dependencies

The Miyoo toolchain sysroot ships SDL 1.2 and FreeType but **not** libzip or libxml2,
and neither does Onion's `lib/`. bebook needs both — libzip to read the epub container,
libxml2 to parse its XHTML — so the headers and prebuilt ARM shared objects are carried
here, as upstream pixel-reader did.

`libz` and `liblzma` are here because libxml2 and libzip link against them.

These are runtime dependencies of bebook alone. They are deliberately *not* installed
into Onion's `lib/`, which is copied wholesale into `.tmp_update/lib` on the device and
is part of the system image rather than any one app.
