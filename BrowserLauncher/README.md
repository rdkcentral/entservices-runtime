# RDK Browser Launcher

# Index

* [Building](#building)

## Building

Configure and build the project:

```bash
mkdir build
cd build
cmake -G 'Ninja' -DCMAKE_MAKE_PROGRAM=ninja -DCMAKE_TOOLCHAIN_FILE=<path to>/toolchain.cmake -DCMAKE_INSTALL_PREFIX:PATH=/runtime ..
DESTDIR=.. cmake --build . -- all
DESTDIR=.. cmake --build . -- install
```
