# TAgged Data Container

Simple data container

## Viewer

Compile the viewer with:

```console
$ c++ -std=c++23 -O3 -o tadc tadc.cc
```

Use the viewer:

```console
$ ./tadc ./tests/new.tadc
```

## Specs

View source code of [`tadc.hh`](code) for binary specification.

## The C++ library

Header only with static (or sometimes inline) implementations [`tadc.hh`](code).
**NOT** a STB style single-header library, but single-header non the less.

### Implementing custom parsers

Info available in the source code [`tadc.hh`](code).

[code]: ./tadc.hh
