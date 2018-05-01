GCC BRIG frontend
=================

Note: As of 2017-01-24 the GCC BRIG frontend has been [upstreamed to
the GCC project](https://gcc.gnu.org/viewcvs/gcc/trunk/gcc/brig/ChangeLog?view=markup&pathrev=244867).
That is, its main development and maintenance will continue in GCC's subversion trunk.

This repository's 'stable' branch will be used to track patches that
have not yet been upstreamed, but have been tested successfully with phsa CPU Agents and the following test suites:

* make check-brig
* HSA\_SUPPORT\_CPU\_DEVICES=1 make -k check -C x86_64-pc-linux-gnu/libgomp

   NOTE: libgomp.c/target-35.c and libgomp.hsa.c/switch-branch-1.c might fail due to known non-BRIGFE related issues.

* [HSA-Conformance-Tests-PRM](https://github.com/parmance/HSA-Conformance-Tests-PRM)
