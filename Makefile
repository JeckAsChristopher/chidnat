# ─── CHN Language Makefile ──────────────────────────────────────────────────
CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2 -g -D_POSIX_C_SOURCE=200809L \
		  -I/usr/include/node \
          -Wno-unused-parameter -Wno-missing-field-initializers
LDFLAGS = -lm /lib/x86_64-linux-gnu/libssl.so.3 /lib/x86_64-linux-gnu/libcrypto.so.3

TARGET  = chn
SRCDIR  = src
OBJDIR  = build

SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

# ─── Build ────────────────────────────────────────────────────────────────────
.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "  LD  $@"
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "  ✓  Built: ./$(TARGET)"

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# ─── Debug build ─────────────────────────────────────────────────────────────
debug: CFLAGS += -DDEBUG -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(TARGET)

# ─── Clean ───────────────────────────────────────────────────────────────────
clean:
	@rm -rf $(OBJDIR) $(TARGET)
	@echo "  ✓  Cleaned"

# ─── Install (optional) ──────────────────────────────────────────────────────
install: $(TARGET)
	@cp $(TARGET) /usr/local/bin/
	@echo "  ✓  Installed to /usr/local/bin/chn"

install-tools:
	@echo "Installing CHN package tools..."
	@cp tools/chn-publish  /usr/local/bin/chn-publish
	@cp tools/chn-install  /usr/local/bin/chn-install
	@cp tools/chn-config   /usr/local/bin/chn-config
	@mkdir -p /usr/local/lib/chn/tools
	@cp tools/lib/common.sh /usr/local/lib/chn/tools/common.sh
	@sed -i 's|SCRIPT_DIR="$$(cd "$$(dirname "$${BASH_SOURCE[0]}")" && pwd)"|SCRIPT_DIR="/usr/local/lib/chn/tools"|' \
		/usr/local/bin/chn-publish \
		/usr/local/bin/chn-install \
		/usr/local/bin/chn-config || true
	@mkdir -p /usr/local/share/chn
	@cp -r chn-libs /usr/local/share/chn/ 2>/dev/null || true
	@echo "  ✓  Installed: chn-publish, chn-install, chn-config"
	@echo "  Run 'chn-config' to set up your registry"

install-all: install install-tools

# ─── Run tests ───────────────────────────────────────────────────────────────
test: $(TARGET)
	@echo "Running tests..."
	@for f in tests/*.chn; do \
		echo "  ▶  $$f"; \
		./$(TARGET) $$f || exit 1; \
	done
	@echo "  ✓  All tests passed"
