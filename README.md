# BRIG gcc frontend for portable HSAIL finalization

The current development branch was forked from the gcc 4.9.1 release tar ball.

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

## Things to do

 * Generate proper symbol information for program linkage variables to enable
   external group and private symbol support. Currently assumes a fully
   linked BRIG program input in this regard.
 * Analyze the call graphs of kernels to get more accurate private and
   group segment usage. Currently assumes the "worst case call graph" by
   accumulating all private and group segment variables to each kernel's
   usage counts.
 * Add phsail-finalizer rt files to the gcc tree so they are built for the
   target. There are built-ins and kernel execution runtime code which should
   be cross-compiled in case of a heterogeneous platform, and gcc has the
   infra in place for cross-compilation.
 * Lots of optimizations to speed up the finalization end result.
 * Upstream the BRIG frontend to gcc: Forward port the patch to the development
   trunk gcc, request for a review, etc.
