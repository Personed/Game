############################################################
#
#  Global GNU Makefile for Archive project
#
#  11.10.2003, PE
#
#  environment: ICC = 1 to use Intel compiler (icc)
#               REDHAT = 1 for the old RedHat system (affects only the linking phase)
#               NORMAL_GCC = 1 for global gcc (else its located in /usr/local/bin)
#               DEBUG, SUPER_RELEASE = 1 .. targets
#               CPU .. if set, used in "-mcpu="
#
############################################################

#  etiquette:

export SHELL = /bin/sh

#  programs:

export ROOT  = $(shell pwd)

# CPU
# export CPU = athlon64

ifdef ICC
  export CC    = icc
  export CCC   = icc
else
ifdef LOCAL_GCC
  export CC    = /usr/local/bin/gcc
  export CCC    = /usr/local/bin/gcc
else
  export CC    = gcc
  export CCC    = g++
endif
endif

export LD    = $(CC)
export AR    = ar rc
export AR2   = ranlib
export RM    = rm -f
export TOUCH = touch
export CP    = cp -f
export LN    = ln -f

#  compiler:
# 
# ifdef CPU
#  CPUFLAGS = -march=$(CPU)
# else
#  CPUFLAGS = -march=pentium3
# endif

CPUFLAGS = -m32 -msse2

ifdef MIMIC_108
export COMMONFLAGS = -DNDEBUG -D_RELEASE=1 -DNET_LOG_COMMAND_LINE -D_LINUX -DMIMIC_108
else
export COMMONFLAGS = -DNDEBUG -D_RELEASE=1 -D_LINUX
endif

ifdef SUPER_RELEASE
ifdef DEBUG_PRELOADING
  export TARGETFLAGS= -g -O2 -fno-strict-aliasing -D_SUPER_RELEASE=1 -DDEBUG_PRELOADING=1
else
  export TARGETFLAGS= -g -O2 -fno-strict-aliasing -D_SUPER_RELEASE=1
endif
  export TARGET= sr
else
ifdef DEBUG
  export TARGETFLAGS= -g
  export TARGET= d
else
#  export TARGETFLAGS= -O2
  export TARGETFLAGS= 
  export TARGET= r
endif
endif

export LIBTARGET = $(ROOT)/lib/$(TARGET)

export GCCFLAGS=    $(CPUFLAGS) -fms-extensions -fno-exceptions -fno-stack-protector -U_FORTIFY_SOURCE -D_GNU_SOURCE
export CPPONLYFLAGS= -Wno-invalid-offsetof -DNOMINMAX=1 -std=c++0x
export INTELFLAGS=  -X -I/opt/intel/compiler60/ia32/include -I/usr/lib/gcc-lib/i386-linux/2.95.2/include -I/usr/include

ifdef ICC
  export CPPFLAGS= $(INTELFLAGS) $(TARGETFLAGS) $(COMMONFLAGS)
else
  export CPPFLAGS= $(GCCFLAGS) $(TARGETFLAGS) $(COMMONFLAGS) -DNO_MMAP
# export CPPFLAGS= $(GCCFLAGS) $(TARGETFLAGS) $(COMMONFLAGS)
endif

#  linker:

//export LDFLAGS= -static -static-libgcc
//export LDFLAGS= -static-libgcc
export LDFLAGS= -m32 -shared-libgcc -Wl,--hash-style=sysv
//export LD_RUN_PATH=armalib

#  sub-targets:

MODULES = RSA/lib ogg/lib Poseidon/Es/lib Poseidon/El/lib
SUBDIRS = 

#  targets:

.PHONY: all start $(MODULES) $(SUBDIRS)

all:    server $(SUBDIRS)

start:
	$(RM) start$(TARGET).txt
	date > start$(TARGET).txt

server: start $(MODULES)
	$(CP) Poseidon/extern/curl-7.27.0/lib/.libs/libcurl.a $(LIBTARGET)
	$(CP) Poseidon/extern/libbotan-1.10.a $(LIBTARGET)
	$(MAKE) -C Poseidon/lib
	$(RM) stop$(TARGET).txt
	date > stop$(TARGET).txt

$(MODULES):
	$(MAKE) -C $@

$(SUBDIRS): $(MODULES)
	$(MAKE) -C $@

.PHONY: clean

clean:
	find . -name "*.d" -exec $(RM) {} \;
	find . -name "*.o" -exec $(RM) {} \;
	find . -name "*~" -exec $(RM) {} \;
	$(RM) bin/serversr
	$(RM) lib/sr/*.a
	$(RM) Poseidon/lib/serversr

