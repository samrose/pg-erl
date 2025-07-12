MODULE_big = erlang_cnode
OBJS = erlang_cnode.o
PG_CPPFLAGS = -I$(ERL_INTERFACE_INCLUDE_DIR)
SHLIB_LINK = -L$(ERL_INTERFACE_LIB_DIR) -lei
EXTENSION = erlang_cnode
DATA = erlang_cnode--1.0.sql erlang_cnode.control
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)

# Override compiler to use gcc on Linux
CC ?= gcc

include $(PGXS)

# Default Erlang interface paths (adjust for your system)
ERL_INTERFACE_INCLUDE_DIR ?= /usr/lib/erlang/usr/include
ERL_INTERFACE_LIB_DIR ?= /usr/lib/erlang/usr/lib

# Alternative paths for different systems
# macOS with Homebrew:
# ERL_INTERFACE_INCLUDE_DIR ?= /usr/local/lib/erlang/usr/include
# ERL_INTERFACE_LIB_DIR ?= /usr/local/lib/erlang/usr/lib

# Ubuntu/Debian:
# ERL_INTERFACE_INCLUDE_DIR ?= /usr/lib/erlang/usr/include
# ERL_INTERFACE_LIB_DIR ?= /usr/lib/erlang/usr/lib

# Fedora/RHEL:
# ERL_INTERFACE_INCLUDE_DIR ?= /usr/lib64/erlang/usr/include
# ERL_INTERFACE_LIB_DIR ?= /usr/lib64/erlang/usr/lib

.PHONY: clean install uninstall

clean:
	rm -f $(OBJS) $(MODULE_big).so

install: all
	$(MAKE) -f $(PGXS) install

uninstall:
	$(MAKE) -f $(PGXS) uninstall 