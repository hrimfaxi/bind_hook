CC ?= gcc
CFLAGS = -Wall -Wextra -fPIC -shared -O2
LDFLAGS = -ldl

TARGET = bind_hook.so
TEST = test_bind

.PHONY: all clean install test

all: $(TARGET) $(TEST)

$(TARGET): bind_hook.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(TEST): test_bind.c
	$(CC) -Wall -O2 -o $@ $<

test: $(TEST) $(TARGET)
	@echo "=== Test 1: Without hook ==="
	./$(TEST) 0.0.0.0:1053 2 < /dev/null
	@echo ""
	@echo "=== Test 2: With hook ==="
	LD_PRELOAD=./$(TARGET) BIND_HOOK_PORT=1053 ./$(TEST) 0.0.0.0:1053 2 < /dev/null

clean:
	rm -f $(TARGET) $(TEST)

install: $(TARGET)
	install -D -m 755 $(TARGET) /usr/lib/$(TARGET)
