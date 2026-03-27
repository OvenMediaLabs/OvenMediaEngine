# Makefile

# Compiler
CC = gcc

# Flags
CFLAGS = -Wall -fPIC
LDFLAGS = -shared

# Source files
SOURCES = libnvidia-ml_stub.c libcuda_stub.c libcudart11_stub.c libcudart12_stub.c libcublas_stub.c libcublasLt_stub.c libxma2api_stub.c libxrm_stub.c libxrt_core_stub.c 

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Output libraries
TARGETS = libnvidia-ml.so.1 libcuda.so.1 libcudart.so.11.0 libcublas.so.11 libcublasLt.so.11 libcudart.so.12 libcublas.so.12 libcublasLt.so.12 libxma2api.so.2 libxrm.so.1 libxrt_core.so.2

# Install path prefix (default: /opt/ovenmediaengine)
PREFIX ?= /opt/ovenmediaengine
INSTALL_PATH = $(PREFIX)/lib/stubs

# Build rules
all: $(TARGETS)

###############################################
# Stubs library for nvidia driver
###############################################
libnvidia-ml.so.1: libnvidia-ml_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libnvidia-ml.so.1 -Wl,--version-script=libnvidia-ml.map -o $@ $^

libcuda.so.1: libcuda_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libcuda.so.1 -Wl,--version-script=libcuda.map -o $@ $^

libcudart.so.11.0: libcudart11_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libcudart.so.11.0 -Wl,--version-script=libcudart11.map -o $@ $^

libcublas.so.11: libcublas_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libcublas.so.11 -Wl,--version-script=libcublas11.map -o $@ $^

libcublasLt.so.11: libcublasLt_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libcublasLt.so.11 -Wl,--version-script=libcublasLt11.map -o $@ $^

libcudart.so.12: libcudart12_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libcudart.so.12 -Wl,--version-script=libcudart12.map -o $@ $^

libcublas.so.12: libcublas_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libcublas.so.12 -Wl,--version-script=libcublas12.map -o $@ $^

libcublasLt.so.12: libcublasLt_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libcublasLt.so.12 -Wl,--version-script=libcublasLt12.map -o $@ $^

###############################################
# Stubs library for xilinx driver
###############################################
libxma2api.so.2: libxma2api_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libxma2api.so.2 -Wl,--version-script=libxma2api.map -o $@ $^

libxrm.so.1: libxrm_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libxrm.so.1 -Wl,--version-script=libxrm.map -o $@ $^

libxrt_core.so.2: libxrt_core_stub.o
	$(CC) $(LDFLAGS) -Wl,-soname,libxrt_core.so.2 -Wl,--version-script=libxrt_core.map -o $@ $^


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGETS)
	mkdir -p $(INSTALL_PATH)
	cp $(TARGETS) $(INSTALL_PATH)

clean:
	rm -f $(OBJECTS) $(TARGETS)

.PHONY: all install clean
