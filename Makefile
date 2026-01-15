BIN ?= octest

SRCDIR ?= src
OBJDIR ?= obj
OUTDIR ?= .

ifneq ($(DEBUG),y)
    OBJDIR := $(OBJDIR)/release
else
    OBJDIR := $(OBJDIR)/debug
endif
ifeq ($(ASAN),y)
    OBJDIR := $(OBJDIR)_asan
endif

SOURCES := $(wildcard $(SRCDIR)/*.c)
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

TARGET := $(OUTDIR)/$(BIN)

CC ?= gcc
LD := $(CC)
_CC := $(TOOLCHAIN)$(CC)
_LD := $(TOOLCHAIN)$(LD)

CFLAGS += -std=c89 -pedantic -Wall -Wextra -Wuninitialized -Wundef -fvisibility=hidden
CPPFLAGS += -D_DEFAULT_SOURCE
LDFLAGS += 
LDLIBS += -lm -lGL -lSDL2

ifeq ($(DEBUG),y)
    CFLAGS += -g -Og -fsanitize=address -Wdouble-promotion
    #CFLAGS += -Wconversion
    LDFLAGS += -fsanitize=address
else
    CFLAGS += -O2
    CPPFLAGS += -DNDEBUG
endif
ifeq ($(ASAN),y)
    CFLAGS += -fsanitize=address
    LDFLAGS += -fsanitize=address
endif

.SECONDEXPANSION:

define mkdir
if [ ! -d '$(1)' ]; then echo 'Creating $(1)/...'; mkdir -p '$(1)'; fi; true
endef
define rm
if [ -f '$(1)' ]; then echo 'Removing $(1)...'; rm -f '$(1)'; fi; true
endef
define rmdir
if [ -d '$(1)' ]; then echo 'Removing $(1)/...'; rm -rf '$(1)'; fi; true
endef

deps.filter := %.c %.h
deps.option := -MM
define deps
$$(filter $$(deps.filter),,$$(shell $(_CC) $(CFLAGS) $(CPPFLAGS) -E $(deps.option) $(1)))
endef

build: $(TARGET)
	@:

run: build
	@echo Running $(BIN)...
	@'$(dir $(BIN))$(notdir $(BIN))' $(RUNFLAGS)

$(OUTDIR):
	@$(call mkdir,$@)

$(OBJDIR):
	@$(call mkdir,$@)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(call deps,$(SRCDIR)/%.c) | $(OBJDIR) $(OUTDIR)
	@echo Compiling $<...
	@$(_CC) $(CFLAGS) -Wall -Wextra $(CPPFLAGS) $< -c -o $@
	@echo Compiled $<

$(TARGET): $(OBJECTS) | $(OUTDIR)
	@echo Linking $@...
	@$(_LD) $(LDFLAGS) $^ $(LDLIBS) -o $@
	@echo Linked $@

clean:
	@$(call rmdir,$(OBJDIR))

distclean: clean
	@$(call rm,$(TARGET))

.PHONY: build run clean distclean
