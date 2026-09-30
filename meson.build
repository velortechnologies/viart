project('viart', 'c', version: '0.1.0',
        default_options: ['c_std=c23', 'warning_level=3', 'werror=true'])

cc = meson.get_compiler('c')
threads = dependency('threads')
includes = include_directories('include', 'src', 'broker')
ws_enabled = get_option('ws')
broker_rpc_enabled = get_option('broker_rpc')
feature_args = [
  '-DVIART_WITH_WS=' + (ws_enabled ? '1' : '0'),
  '-DVIART_WITH_BROKER_RPC=' + (broker_rpc_enabled ? '1' : '0'),
]
crypto_deps = []
if ws_enabled
  crypto_deps = [cc.find_library('ssl', required: true),
                 cc.find_library('crypto', required: true)]
endif

client_sources = files('src/wire.c', 'src/handshake.c', 'src/queue.c',
                       'src/pending.c', 'src/transport.c', 'src/reactor.c',
                       'src/client.c', 'src/rpc_wire.c', 'src/rpc.c')
if ws_enabled
  client_sources += files('src/ws_bridge.c')
endif
client_lib = static_library('viart', client_sources,
                            include_directories: includes,
                            c_args: feature_args,
                            dependencies: threads,
                            install: true)
client_dep = declare_dependency(link_with: client_lib,
                                include_directories: includes,
                                dependencies: [threads] + crypto_deps)
install_headers('include/viart.h', 'include/viart_rpc.h')

broker_sources = files('broker/server.c', 'broker/protocol.c',
                       'broker/subscriptions.c', 'broker/index.c',
                       'broker/config.c', 'broker/acl.c', 'src/wire.c')
if ws_enabled
  broker_sources += files('broker/ws.c', 'broker/auth.c')
endif
if broker_rpc_enabled
  broker_sources += files('broker/msgpack.c', 'broker/rpc.c', 'src/rpc_wire.c')
endif
executable('viartd', broker_sources,
           include_directories: includes,
           c_args: feature_args,
           dependencies: crypto_deps,
           install: true)

if get_option('demo')
  executable('viart-demo', 'examples/demo.c',
             dependencies: client_dep, install: true)
endif
if get_option('cli')
  executable('viart-cli',
             files('cli/main.c', 'cli/common.c', 'cli/payload.c',
                   'cli/benchmark.c', 'broker/msgpack.c'),
             dependencies: client_dep, install: true)
endif

foreach name : ['wire', 'handshake', 'queue', 'pending', 'transport', 'reactor']
  runner = executable('test_' + name, 'tests/test_' + name + '.c',
                      dependencies: client_dep)
  test(name, runner)
endforeach

runner = executable('test_rpc_wire',
                    files('tests/test_rpc_wire.c', 'src/rpc_wire.c', 'src/wire.c'),
                    include_directories: includes)
test('rpc_wire', runner)

foreach name : ['protocol', 'subscriptions', 'config', 'index', 'acl']
  sources = ['broker/test_' + name + '.c', 'broker/' + name + '.c']
  if name == 'protocol'
    sources += ['src/wire.c']
  elif name == 'index' or name == 'acl'
    sources += ['broker/subscriptions.c']
  endif
  runner = executable('test_broker_' + name, sources,
                      include_directories: includes)
  test('broker_' + name, runner)
endforeach

if broker_rpc_enabled
  runner = executable('test_broker_msgpack',
                      files('broker/test_msgpack.c', 'broker/msgpack.c'),
                      include_directories: includes)
  test('broker_msgpack', runner)
  runner = executable('test_broker_rpc',
                      files('broker/test_rpc.c', 'broker/rpc.c',
                            'broker/msgpack.c', 'src/rpc_wire.c', 'src/wire.c'),
                      include_directories: includes)
  test('broker_rpc', runner)
endif

if ws_enabled
  runner = executable('test_broker_ws',
                      files('broker/test_ws.c', 'broker/ws.c'),
                      include_directories: includes,
                      dependencies: crypto_deps)
  test('broker_ws', runner)
  runner = executable('test_broker_auth',
                      files('broker/test_auth.c', 'broker/auth.c'),
                      include_directories: includes,
                      dependencies: crypto_deps)
  test('broker_auth', runner)
endif
