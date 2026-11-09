.DEFAULT_GOAL := all
PYTHON ?= python3
JOBS ?= 4
BUILD_CC ?= cc
MPIEXEC ?=
HPL_CFLAGS ?= -O3
OPENBLAS_TARGET ?= auto
OPENBLAS_ARCHIVE ?=
CPU_LIST ?= 0-7
P ?= 2
Q ?= 4
NB ?= 192
N ?= auto
RAM_GIB ?= 32
RESERVE_GIB ?= 2
AVAILABLE_RESERVE_GIB ?= 1
ALLOW_HOST ?= 0
export JOBS BUILD_CC MPIEXEC OPENBLAS_TARGET OPENBLAS_ARCHIVE CPU_LIST P Q NB N
export MPICH_ARCHIVE HPL_PROFILE CROSS_HOST AR RANLIB HPL_CFLAGS
export RAM_GIB RESERVE_GIB AVAILABLE_RESERVE_GIB ALLOW_HOST
.PHONY: all package config plan run smoke doctor test clean help
all:
	$(PYTHON) hpl.py build
package: all
	$(PYTHON) package.py
config:
	$(PYTHON) hpl.py config
plan:
	$(PYTHON) hpl.py plan
run: all
	$(PYTHON) hpl.py run
smoke: all
	$(PYTHON) hpl.py run --smoke
# This reports dependencies; it does not install packages or change the system.
doctor:
	$(PYTHON) hpl.py doctor
test:
	$(PYTHON) -m unittest discover -s tests -v
clean:
	$(PYTHON) hpl.py clean
help:
	@echo 'make: build HPL 2.3 + static MPICH 4.3.2 + single-thread OpenBLAS 0.3.34'
	@echo 'make plan/config: compute maximum N / write .build/run/HPL.dat'
	@echo 'make smoke/run: residual-checked small / maximum-memory benchmark'
	@echo 'Defaults: X100 CPU_LIST=0-7 P=2 Q=4 NB=192 RAM_GIB=32 N=auto'
	@echo 'Optional: NB=256, N=12000, OPENBLAS_ARCHIVE=/path/to/release.tar.gz'
	@echo 'make test / doctor / clean (results and vendored package are preserved)'
