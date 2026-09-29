CC = clang-21
AR ?= ar
CFLAGS ?= -std=c23 -O2 -g -Wall -Wextra -Wpedantic -Werror
CPPFLAGS += -Iinclude -Isrc
LDFLAGS += -pthread -lssl -lcrypto

MODULES = wire handshake queue pending transport ws_bridge reactor client rpc_wire rpc
OBJECTS = $(addprefix build/,$(addsuffix .o,$(MODULES)))
TESTS = test_wire test_handshake test_queue test_pending test_transport test_reactor

.PHONY: all test integration clean
all: build/libviart.a build/viart-demo build/viart-cli

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -pthread -c $< -o $@

build/libviart.a: $(OBJECTS)
	$(AR) rcs $@ $^

build/viart-demo: examples/demo.c build/libviart.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libviart.a $(LDFLAGS) -o $@

build/viart-cli: cli/main.c cli/common.c cli/payload.c cli/benchmark.c broker/msgpack.c build/libviart.a
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ $(LDFLAGS) -o $@

.PHONY: cli-test
cli-test: build/viart-cli
	python3 cli/test_cli.py
	python3 cli/test_wss.py

build/viart-cli-asan: cli/main.c cli/common.c cli/payload.c cli/benchmark.c broker/msgpack.c $(addprefix src/,$(addsuffix .c,$(MODULES)))
	$(CC) $(CPPFLAGS) -Ibroker -std=c23 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $^ $(LDFLAGS) -o $@

.PHONY: cli-sanitize-test
cli-sanitize-test: build/viart-cli-asan
	VIART_CLI_BINARY=build/viart-cli-asan python3 cli/test_cli.py
	ASAN_OPTIONS=detect_leaks=0 VIART_CLI_BINARY=build/viart-cli-asan python3 cli/test_wss.py

build/test_%: tests/test_%.c build/libviart.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libviart.a $(LDFLAGS) -o $@

test: $(addprefix build/,$(TESTS))
	@for t in $(TESTS); do echo "== $$t =="; ./build/$$t; done

integration: build/test_client
	./build/test_client

clean:
	$(RM) -r build

build/viartd: broker/server.c broker/protocol.c broker/subscriptions.c broker/index.c broker/config.c broker/msgpack.c broker/rpc.c broker/acl.c broker/auth.c broker/ws.c src/rpc_wire.c src/wire.c | build
	$(CC) $(CPPFLAGS) -Ibroker -std=c23 -O2 -Wall -Wextra -Wpedantic -Werror $^ -lssl -lcrypto -s -o $@

build/test_broker_ws: broker/test_ws.c broker/ws.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -lcrypto -o $@

build/test_broker_auth: broker/test_auth.c broker/auth.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -lcrypto -o $@

build/test_broker_protocol: broker/test_protocol.c broker/protocol.c src/wire.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -o $@

build/test_broker_subscriptions: broker/test_subscriptions.c broker/subscriptions.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -o $@

.PHONY: broker broker-test
broker: build/viartd
broker-test: build/test_broker_protocol build/test_broker_subscriptions build/test_broker_config build/test_broker_index build/test_broker_msgpack build/test_broker_rpc build/test_broker_acl build/test_broker_ws build/test_broker_auth
	./build/test_broker_protocol
	./build/test_broker_subscriptions
	./build/test_broker_config
	./build/test_broker_index
	./build/test_broker_msgpack
	./build/test_broker_rpc
	./build/test_broker_acl
	./build/test_broker_ws
	./build/test_broker_auth

build/test_broker_acl: broker/test_acl.c broker/acl.c broker/subscriptions.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -o $@

.PHONY: broker-integration
broker-integration: build/viartd
	python3 broker/test_integration.py

build/test_broker_config: broker/test_config.c broker/config.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -o $@


.PHONY: broker-limits
broker-limits: build/viartd
	python3 broker/test_limits.py

build/test_broker_index: broker/test_index.c broker/index.c broker/subscriptions.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -o $@

.PHONY: broker-load
broker-load: build/viartd
	python3 broker/test_load.py
	python3 broker/test_batch_partial.py

build/test_rpc_wire: tests/test_rpc_wire.c src/rpc_wire.c src/wire.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

.PHONY: rpc-test
rpc-test: build/test_rpc_wire
	./build/test_rpc_wire

build/test_broker_msgpack: broker/test_msgpack.c broker/msgpack.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -o $@

build/test_broker_rpc: broker/test_rpc.c broker/rpc.c broker/msgpack.c src/rpc_wire.c src/wire.c
	$(CC) $(CPPFLAGS) -Ibroker $(CFLAGS) $^ -o $@

.PHONY: broker-rpc-integration
broker-rpc-integration: build/viartd
	python3 broker/test_rpc_integration.py

.PHONY: broker-rpc-events
broker-rpc-events: build/viartd
	python3 broker/test_rpc_events.py

.PHONY: broker-acl-integration
broker-acl-integration: build/viartd
	python3 broker/test_acl_integration.py

.PHONY: broker-wss-integration
broker-wss-integration: build/viartd build/test_rpc_client build/test_ws_security
	python3 broker/test_wss_integration.py

.PHONY: broker-transports-load
broker-transports-load: build/viartd
	python3 broker/test_transports_load.py

.PHONY: broker-churn
broker-churn: build/viartd
	python3 broker/test_churn.py

-include $(OBJECTS:.o=.d)

build/test_rpc_client: tests/test_rpc_client.c build/libviart.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libviart.a $(LDFLAGS) -o $@

build/test_ws_security: tests/test_ws_security.c build/libviart.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libviart.a $(LDFLAGS) -o $@

.PHONY: rpc-integration
rpc-integration: build/test_rpc_client
	./build/test_rpc_client
