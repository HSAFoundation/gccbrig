# BRIG gcc frontend for portable HSAIL finalization

## Building and installation

### Users

Users of better install gccbrig somewhere in PATH and LD_LIBRARY_PATH:

```
  cd gccbrig
  mkdir build
  cd build
  ../configure --disable-multilib --enable-languages=brig --prefix=/your/install/prefix
  make -j4 && make install
```

### Developers

It's handiest to use gccbrig from the build tree during development as
there's no need to reinstall it after building new modifications for
testing. Also, --enable-checking should be added for extra GENERIC etc. checks
which can reveal bugs.

```
  cd gccbrig
  mkdir build
  cd build
  ../configure --enable-checking --disable-multilib --enable-languages=brig
  make -j4
```

