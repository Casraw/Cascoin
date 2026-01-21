# Copyright (c) 2013-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

DIST_SUBDIRS = secp256k1 univalue

AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS)
AM_CXXFLAGS = $(HARDENED_CXXFLAGS) $(ERROR_CXXFLAGS)
AM_CPPFLAGS = $(HARDENED_CPPFLAGS)
EXTRA_LIBRARIES =

if EMBEDDED_UNIVALUE
LIBUNIVALUE = univalue/libunivalue.la

$(LIBUNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)
else
LIBUNIVALUE = $(UNIVALUE_LIBS)
endif

BITCOIN_INCLUDES=-I$(builddir) $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS) $(EVMC_CFLAGS) $(SQLITE3_CFLAGS)

BITCOIN_INCLUDES += -I$(srcdir)/secp256k1/include
BITCOIN_INCLUDES += $(UNIVALUE_CFLAGS)

LIBBITCOIN_SERVER=libbitcoin_server.a
LIBBITCOIN_COMMON=libbitcoin_common.a
LIBBITCOIN_CONSENSUS=libbitcoin_consensus.a
LIBBITCOIN_CLI=libbitcoin_cli.a
LIBBITCOIN_UTIL=libbitcoin_util.a
LIBBITCOIN_CRYPTO=crypto/libbitcoin_crypto.a
LIBBITCOINQT=qt/libbitcoinqt.a
LIBSECP256K1=secp256k1/libsecp256k1.la

if ENABLE_ZMQ
LIBBITCOIN_ZMQ=libbitcoin_zmq.a
endif
if BUILD_BITCOIN_LIBS
LIBBITCOINCONSENSUS=libbitcoinconsensus.la
endif
if ENABLE_WALLET
LIBBITCOIN_WALLET=libbitcoin_wallet.a
endif

$(LIBSECP256K1): $(wildcard secp256k1/src/*) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_CLI) \
  $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_ZMQ)

lib_LTLIBRARIES = $(LIBBITCOINCONSENSUS)

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =
BENCHMARKS =

if BUILD_BITCOIND
  bin_PROGRAMS += cascoind
endif

if BUILD_BITCOIN_UTILS
  bin_PROGRAMS += cascoin-cli cascoin-tx
endif

.PHONY: FORCE check-symbols check-security
# bitcoin core #
# Cascoin: Rialto: Added rialto.h
BITCOIN_CORE_H = \
  addrdb.h \
  addrman.h \
  base58.h \
  bech32.h \
  bloom.h \
  blockencodings.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkqueue.h \
  clientversion.h \
  coins.h \
  compat.h \
  compat/byteswap.h \
  compat/endian.h \
  compat/sanity.h \
  compressor.h \
  consensus/consensus.h \
  consensus/tx_verify.h \
  core_io.h \
  core_memusage.h \
  cuckoocache.h \
  fs.h \
  httprpc.h \
  httpserver.h \
  httpserver/cvmdashboard.h \
  httpserver/cvmdashboard_html.h \
  httpserver/cvmdashboard_evm.h \
  httpserver/l2_dashboard.h \
  httpserver/l2_dashboard_html.h \
  httpserver/l2_websocket.h \
  httpserver/reward_dashboard.h \
  httpserver/reward_dashboard_html.h \
  indirectmap.h \
  init.h \
  key.h \
  keystore.h \
  dbwrapper.h \
  limitedmap.h \
  memusage.h \
  merkleblock.h \
  miner.h \
  net.h \
  net_processing.h \
  netaddress.h \
  netbase.h \
  netmessagemaker.h \
  noui.h \
  policy/feerate.h \
  policy/fees.h \
  policy/policy.h \
  policy/rbf.h \
  pow.h \
  protocol.h \
  random.h \
  reverse_iterator.h \
  reverselock.h \
  rpc/blockchain.h \
  rpc/client.h \
  rpc/mining.h \
  rpc/protocol.h \
  rpc/safemode.h \
  rpc/server.h \
  rpc/register.h \
  rpc/util.h \
  scheduler.h \
  script/sigcache.h \
  script/sign.h \
  script/standard.h \
  script/ismine.h \
  streams.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/lockedpool.h \
  sync.h \
  threadsafety.h \
  threadinterrupt.h \
  timedata.h \
  torcontrol.h \
  txdb.h \
  txmempool.h \
  ui_interface.h \
  undo.h \
  util.h \
  utilmoneystr.h \
  utiltime.h \
  validation.h \
  validationinterface.h \
  versionbits.h \
  wallet/coincontrol.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/feebumper.h \
  wallet/fees.h \
  wallet/init.h \
  wallet/rpcwallet.h \
  wallet/wallet.h \
  wallet/walletdb.h \
  wallet/walletutil.h \
  warnings.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h\
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h \
  rialto.h \
  beenft.h \
  bctdb.h \
  cvm/cvm.h \
  cvm/vmstate.h \
  cvm/opcodes.h \
  cvm/contract.h \
  cvm/cvmdb.h \
  cvm/reputation.h \
  cvm/softfork.h \
  cvm/txbuilder.h \
  cvm/blockprocessor.h \
  cvm/trustgraph.h \
  cvm/behaviormetrics.h \
  cvm/graphanalysis.h \
  cvm/securehat.h \
  cvm/walletcluster.h \
  cvm/cvmtx.h \
  cvm/evmc_host.h \
  cvm/trust_context.h \
  cvm/bytecode_detector.h \
  cvm/enhanced_vm.h \
  cvm/evm_engine.h \
  cvm/enhanced_storage.h \
  cvm/sustainable_gas.h \
  cvm/gas_allowance.h \
  cvm/tx_priority.h \
  cvm/gas_subsidy.h \
  cvm/reputation_signature.h \
  cvm/resource_manager.h \
  cvm/cleanup_manager.h \
  cvm/mempool_manager.h \
  cvm/fee_calculator.h \
  cvm/mempool_priority.h \
  cvm/block_validator.h \
  cvm/evm_rpc.h \
  cvm/validator_keys.h \
  cvm/execution_tracer.h \
  cvm/validator_attestation.h \
  cvm/vote_manipulation_detector.h \
  cvm/trust_graph_manipulation_detector.h \
  cvm/validator_compensation.h \
  cvm/trust_attestation.h \
  cvm/cross_chain_bridge.h \
  cvm/contract_state_sync.h \
  cvm/consensus_safety.h \
  cvm/trust_graph_sync.h \
  cvm/security_audit.h \
  cvm/anomaly_detector.h \
  cvm/security_rpc.h \
  cvm/access_control_audit.h \
  cvm/backward_compat.h \
  cvm/dos_protection.h \
  cvm/metrics.h \
  cvm/graceful_degradation.h \
  cvm/reward_types.h \
  cvm/commit_reveal.h \
  cvm/reward_distributor.h \
  l2/l2_common.h \
  l2/l2_chainparams.h \
  l2/l2_config.h \
  l2/l2_block.h \
  l2/l2_block_validator.h \
  l2/l2_registry.h \
  l2/l2_reputation.h \
  l2/l2_transaction.h \
  l2/bridge_contract.h \
  l2/burn_parser.h \
  l2/burn_registry.h \
  l2/burn_validator.h \
  l2/challenge_handler.h \
  l2/cross_layer_messaging.h \
  l2/data_availability.h \
  l2/encrypted_mempool.h \
  l2/fee_distributor.h \
  l2/forced_inclusion.h \
  l2/fraud_proof.h \
  l2/l2_minter.h \
  l2/leader_election.h \
  l2/mint_consensus.h \
  l2/rate_limiter.h \
  l2/reorg_monitor.h \
  l2/sequencer_consensus.h \
  l2/sequencer_discovery.h \
  l2/sparse_merkle_tree.h \
  l2/account_state.h \
  l2/state_manager.h \
  l2/l2_token_manager.h \
  l2/l2_faucet.h \
  l2/l2_peer_manager.h \
  l2/timestamp_validator.h \
  l2/collusion_detector.h \
  l2/security_monitor.h


obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh "$(abs_top_builddir)/src/obj/build.h" \
	  "$(abs_top_srcdir)"
libbitcoin_util_a-clientversion.$(OBJEXT): obj/build.h

# server: shared between bitcoind and bitcoin-qt
# Cascoin: Rialto: Added rialto.cpp
libbitcoin_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(MINIUPNPC_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libbitcoin_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_server_a_SOURCES = \
  addrdb.cpp \
  addrman.cpp \
  bloom.cpp \
  blockencodings.cpp \
  chain.cpp \
  checkpoints.cpp \
  consensus/tx_verify.cpp \
  httprpc.cpp \
  httpserver.cpp \
  httpserver/cvmdashboard.cpp \
  httpserver/l2_dashboard.cpp \
  httpserver/l2_websocket.cpp \
  httpserver/reward_dashboard.cpp \
  init.cpp \
  dbwrapper.cpp \
  merkleblock.cpp \
  miner.cpp \
  net.cpp \
  net_processing.cpp \
  noui.cpp \
  policy/fees.cpp \
  policy/policy.cpp \
  policy/rbf.cpp \
  pow.cpp \
  rest.cpp \
  rpc/blockchain.cpp \
  rpc/mining.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/safemode.cpp \
  rpc/server.cpp \
  script/sigcache.cpp \
  script/ismine.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  ui_interface.cpp \
  validation.cpp \
  validationinterface.cpp \
  versionbits.cpp \
  rialto.cpp \
  beenft.cpp \
  bctdb.cpp \
  cvm/cvm.cpp \
  cvm/vmstate.cpp \
  cvm/opcodes.cpp \
  cvm/contract.cpp \
  cvm/cvmdb.cpp \
  cvm/reputation.cpp \
  cvm/softfork.cpp \
  cvm/txbuilder.cpp \
  cvm/blockprocessor.cpp \
  cvm/trustgraph.cpp \
  cvm/behaviormetrics.cpp \
  cvm/graphanalysis.cpp \
  cvm/securehat.cpp \
  cvm/walletcluster.cpp \
  cvm/cvmtx.cpp \
  cvm/evmc_host.cpp \
  cvm/trust_context.cpp \
  cvm/bytecode_detector.cpp \
  cvm/enhanced_vm.cpp \
  cvm/evm_engine.cpp \
  cvm/enhanced_storage.cpp \
  cvm/sustainable_gas.cpp \
  cvm/gas_allowance.cpp \
  cvm/tx_priority.cpp \
  cvm/gas_subsidy.cpp \
  cvm/reputation_signature.cpp \
  cvm/resource_manager.cpp \
  cvm/cleanup_manager.cpp \
  cvm/mempool_manager.cpp \
  cvm/fee_calculator.cpp \
  cvm/mempool_priority.cpp \
  cvm/block_validator.cpp \
  cvm/activation.cpp \
  cvm/receipt.cpp \
  cvm/address_index.cpp \
  cvm/nonce_manager.cpp \
  cvm/evm_rpc.cpp \
  cvm/hat_consensus.cpp \
  cvm/eclipse_sybil_protection.cpp \
  cvm/consensus_validator.cpp \
  cvm/trust_attestation.cpp \
  cvm/cross_chain_bridge.cpp \
  cvm/validator_attestation.cpp \
  cvm/validator_keys.cpp \
  cvm/execution_tracer.cpp \
  cvm/validator_compensation.cpp \
  cvm/vote_manipulation_detector.cpp \
  cvm/trust_graph_manipulation_detector.cpp \
  cvm/contract_state_sync.cpp \
  cvm/consensus_safety.cpp \
  cvm/trust_graph_sync.cpp \
  cvm/security_audit.cpp \
  cvm/anomaly_detector.cpp \
  cvm/security_rpc.cpp \
  cvm/access_control_audit.cpp \
  cvm/backward_compat.cpp \
  cvm/dos_protection.cpp \
  cvm/metrics.cpp \
  cvm/graceful_degradation.cpp \
  cvm/reward_types.cpp \
  cvm/commit_reveal.cpp \
  cvm/reward_distributor.cpp \
  l2/l2_common.cpp \
  l2/l2_chainparams.cpp \
  l2/l2_config.cpp \
  l2/l2_block.cpp \
  l2/l2_block_validator.cpp \
  l2/l2_registry.cpp \
  l2/l2_reputation.cpp \
  l2/l2_transaction.cpp \
  l2/bridge_contract.cpp \
  l2/burn_parser.cpp \
  l2/burn_registry.cpp \
  l2/burn_validator.cpp \
  l2/challenge_handler.cpp \
  l2/cross_layer_messaging.cpp \
  l2/data_availability.cpp \
  l2/encrypted_mempool.cpp \
  l2/fee_distributor.cpp \
  l2/forced_inclusion.cpp \
  l2/fraud_proof.cpp \
  l2/l2_minter.cpp \
  l2/leader_election.cpp \
  l2/mint_consensus.cpp \
  l2/rate_limiter.cpp \
  l2/reorg_monitor.cpp \
  l2/sequencer_consensus.cpp \
  l2/sequencer_discovery.cpp \
  l2/sparse_merkle_tree.cpp \
  l2/state_manager.cpp \
  l2/l2_token_manager.cpp \
  l2/l2_faucet.cpp \
  l2/l2_peer_manager.cpp \
  l2/timestamp_validator.cpp \
  l2/collusion_detector.cpp \
  l2/security_monitor.cpp \
  rpc/cvm.cpp \
  rpc/l2.cpp \
  rpc/l2_burn.cpp \
  $(BITCOIN_CORE_H)

if ENABLE_ZMQ
libbitcoin_zmq_a_CPPFLAGS = $(BITCOIN_INCLUDES) $(ZMQ_CFLAGS)
libbitcoin_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp
endif


# wallet: shared between bitcoind and bitcoin-qt, but only linked
# when wallet enabled
libbitcoin_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_wallet_a_SOURCES = \
  wallet/crypter.cpp \
  wallet/db.cpp \
  wallet/feebumper.cpp \
  wallet/fees.cpp \
  wallet/init.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcwallet.cpp \
  wallet/wallet.cpp \
  wallet/walletdb.cpp \
  wallet/walletutil.cpp \
  beenft.cpp \
  $(BITCOIN_CORE_H)

# crypto primitives library
# Cascoin: MinotaurX: Added crypto/minotaurx/
crypto_libbitcoin_crypto_a_CPPFLAGS = $(AM_CPPFLAGS) $(SSL_CFLAGS)
crypto_libbitcoin_crypto_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libbitcoin_crypto_a_SOURCES = \
  crypto/aes.cpp \
  crypto/aes.h \
  crypto/chacha20.h \
  crypto/chacha20.cpp \
  crypto/common.h \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/ripemd160.cpp \
  crypto/ripemd160.h \
  crypto/scrypt.cpp \
  crypto/scrypt-sse2.cpp \
  crypto/scrypt.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha512.cpp \
  crypto/sha512.h \
  crypto/minotaurx/sph_blake.h \
  crypto/minotaurx/sph_cubehash.h \
  crypto/minotaurx/sph_echo.h \
  crypto/minotaurx/sph_fugue.h \
  crypto/minotaurx/sph_groestl.h \
  crypto/minotaurx/sph_hamsi.h \
  crypto/minotaurx/sph_haval.h \
  crypto/minotaurx/sph_jh.h \
  crypto/minotaurx/sph_keccak.h \
  crypto/minotaurx/sph_luffa.h \
  crypto/minotaurx/sph_sha2.h \
  crypto/minotaurx/sph_sha2big.c \
  crypto/minotaurx/sph_shabal.h \
  crypto/minotaurx/sph_shavite.h \
  crypto/minotaurx/sph_simd.h \
  crypto/minotaurx/sph_skein.h \
  crypto/minotaurx/sph_types.h \
  crypto/minotaurx/sph_whirlpool.h \
  crypto/minotaurx/bmw.c \
  crypto/minotaurx/whirlpool.c \
  crypto/minotaurx/Sponge.h \
  crypto/minotaurx/blake.c \
  crypto/minotaurx/blake2.h \
  crypto/minotaurx/blake2-impl.h \
  crypto/minotaurx/blake2s-ref.c \
  crypto/minotaurx/cubehash.c \
  crypto/minotaurx/echo.c \
  crypto/minotaurx/fugue.c \
  crypto/minotaurx/groestl.c \
  crypto/minotaurx/hamsi.c \
  crypto/minotaurx/hashblake.h \
  crypto/minotaurx/hashgroestl.h \
  crypto/minotaurx/hashquark.h \
  crypto/minotaurx/hashqubit.h \
  crypto/minotaurx/hashskein.h \
  crypto/minotaurx/hashx11.h \
  crypto/minotaurx/hashx13.h \
  crypto/minotaurx/hashx15.h \
  crypto/minotaurx/hashx17.h \
  crypto/minotaurx/haval.c \
  crypto/minotaurx/jh.c \
  crypto/minotaurx/keccak.c \
  crypto/minotaurx/luffa.c \
  crypto/minotaurx/Lyra2.c \
  crypto/minotaurx/Lyra2.h \
  crypto/minotaurx/Lyra2RE.c \
  crypto/minotaurx/Lyra2RE.h \
  crypto/minotaurx/shabal.c \
  crypto/minotaurx/shavite.c \
  crypto/minotaurx/simd.c \
  crypto/minotaurx/skein.c \
  crypto/minotaurx/Sponge.c \
  crypto/minotaurx/sph_bmw.h \
  crypto/minotaurx/minotaur.h \
  crypto/minotaurx/yespower/yespower.c \
  crypto/minotaurx/yespower/yespower.h \
  crypto/minotaurx/yespower/crypto/sha256.c \
  crypto/minotaurx/yespower/crypto/sha256.h \
  crypto/minotaurx/yespower/util/insecure_memzero.h \
  crypto/minotaurx/yespower/util/sysendian.h

if USE_ASM
crypto_libbitcoin_crypto_a_SOURCES += crypto/sha256_sse4.cpp
endif

# consensus: shared between all executables that validate any consensus rules.
libbitcoin_consensus_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_consensus_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_consensus_a_SOURCES = \
  amount.h \
  arith_uint256.cpp \
  arith_uint256.h \
  consensus/merkle.cpp \
  consensus/merkle.h \
  consensus/params.h \
  consensus/validation.h \
  hash.cpp \
  hash.h \
  prevector.h \
  primitives/block.h \
  primitives/transaction.cpp \
  primitives/transaction.h \
  pubkey.cpp \
  pubkey.h \
  script/bitcoinconsensus.cpp \
  script/interpreter.cpp \
  script/interpreter.h \
  script/script.cpp \
  script/script.h \
  script/script_error.cpp \
  script/script_error.h \
  serialize.h \
  tinyformat.h \
  uint256.cpp \
  uint256.h \
  utilstrencodings.cpp \
  utilstrencodings.h \
  version.h

# common: shared between bitcoind, and bitcoin-qt and non-server tools
libbitcoin_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_common_a_SOURCES = \
  base58.cpp \
  bech32.cpp \
  chainparams.cpp \
  coins.cpp \
  compressor.cpp \
  core_read.cpp \
  core_write.cpp \
  key.cpp \
  keystore.cpp \
  netaddress.cpp \
  netbase.cpp \
  policy/feerate.cpp \
  primitives/block.cpp \
  protocol.cpp \
  scheduler.cpp \
  script/sign.cpp \
  script/standard.cpp \
  warnings.cpp \
  $(BITCOIN_CORE_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libbitcoin_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_util_a_SOURCES = \
  support/lockedpool.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  compat/strnlen.cpp \
  fs.cpp \
  random.cpp \
  rpc/protocol.cpp \
  rpc/util.cpp \
  support/cleanse.cpp \
  sync.cpp \
  threadinterrupt.cpp \
  util.cpp \
  utilmoneystr.cpp \
  utilstrencodings.cpp \
  utiltime.cpp \
  $(BITCOIN_CORE_H)

if GLIBC_BACK_COMPAT
libbitcoin_util_a_SOURCES += compat/glibc_compat.cpp
endif

# cli: shared between bitcoin-cli and bitcoin-qt
libbitcoin_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_cli_a_SOURCES = \
  rpc/client.cpp \
  $(BITCOIN_CORE_H)

nodist_libbitcoin_util_a_SOURCES = $(srcdir)/obj/build.h
#

# bitcoind binary #
cascoind_SOURCES = bitcoind.cpp
cascoind_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
cascoind_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
cascoind_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
cascoind_SOURCES += bitcoind-res.rc
endif

cascoind_LDADD = \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_ZMQ) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV) \
  $(LIBSECP256K1)

cascoind_LDADD += $(BOOST_LIBS) $(BDB_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(MINIUPNPC_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(ZMQ_LIBS) $(SQLITE3_LIBS)

if ENABLE_EVMC
cascoind_LDADD += -levmone $(EVMONE_PRECOMPILES_LIBS) -levmc-loader -levmc-instructions
endif

# bitcoin-cli binary #
cascoin_cli_SOURCES = bitcoin-cli.cpp
cascoin_cli_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
cascoin_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
cascoin_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
cascoin_cli_SOURCES += bitcoin-cli-res.rc
endif

cascoin_cli_LDADD = \
  $(LIBBITCOIN_CLI) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO)

cascoin_cli_LDADD += $(BOOST_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(EVENT_LIBS)

if ENABLE_EVMC
cascoin_cli_LDADD += -levmone $(EVMONE_PRECOMPILES_LIBS) -levmc-loader -levmc-instructions
endif
#

# bitcoin-tx binary #
cascoin_tx_SOURCES = bitcoin-tx.cpp
cascoin_tx_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
cascoin_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
cascoin_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
cascoin_tx_SOURCES += bitcoin-tx-res.rc
endif

cascoin_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBSECP256K1)

cascoin_tx_LDADD += $(BOOST_LIBS) $(CRYPTO_LIBS)

if ENABLE_EVMC
cascoin_tx_LDADD += -levmone $(EVMONE_PRECOMPILES_LIBS) -levmc-loader -levmc-instructions
endif
#

# bitcoinconsensus library #
if BUILD_BITCOIN_LIBS
include_HEADERS = script/bitcoinconsensus.h
libbitcoinconsensus_la_SOURCES = $(crypto_libbitcoin_crypto_a_SOURCES) $(libbitcoin_consensus_a_SOURCES)

if GLIBC_BACK_COMPAT
  libbitcoinconsensus_la_SOURCES += compat/glibc_compat.cpp
endif

libbitcoinconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libbitcoinconsensus_la_LIBADD = $(LIBSECP256K1) $(CRYPTO_LIBS)

if ENABLE_EVMC
libbitcoinconsensus_la_LIBADD += -levmone $(EVMONE_PRECOMPILES_LIBS) -levmc-loader -levmc-instructions
endif
libbitcoinconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_BITCOIN_INTERNAL $(SSL_CFLAGS)
libbitcoinconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

endif
#

CTAES_DIST =  crypto/ctaes/bench.c
CTAES_DIST += crypto/ctaes/ctaes.c
CTAES_DIST += crypto/ctaes/ctaes.h
CTAES_DIST += crypto/ctaes/README.md
CTAES_DIST += crypto/ctaes/test.c

# Cascoin: MinotaurX
CRYPTO_HELPER_DIST =  crypto/minotaurx/aes_helper.c
CRYPTO_HELPER_DIST += crypto/minotaurx/hamsi_helper.c
CRYPTO_HELPER_DIST += crypto/minotaurx/haval_helper.c
CRYPTO_HELPER_DIST += crypto/minotaurx/md_helper.c
CRYPTO_HELPER_DIST += crypto/minotaurx/sph_md_helper.c

CLEANFILES = $(EXTRA_LIBRARIES)

CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += consensus/*.gcda consensus/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += policy/*.gcda policy/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += support/*.gcda support/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += wallet/*.gcda wallet/*.gcno
CLEANFILES += wallet/test/*.gcda wallet/test/*.gcno
CLEANFILES += zmq/*.gcda zmq/*.gcno
CLEANFILES += obj/build.h

# Cascoin: MinotaurX
EXTRA_DIST = $(CTAES_DIST) $(CRYPTO_HELPER_DIST)


config/bitcoin-config.h: config/stamp-h1
	@$(MAKE) -C $(top_builddir) $(subdir)/$(@)
config/stamp-h1: $(top_srcdir)/$(subdir)/config/bitcoin-config.h.in $(top_builddir)/config.status
	$(AM_V_at)$(MAKE) -C $(top_builddir) $(subdir)/$(@)
$(top_srcdir)/$(subdir)/config/bitcoin-config.h.in:  $(am__configure_deps)
	$(AM_V_at)$(MAKE) -C $(top_srcdir) $(subdir)/config/bitcoin-config.h.in

clean-local:
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C univalue clean
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -f config.h
	-rm -rf test/__pycache__

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(CPPFLAGS) -DWINDRES_PREPROC -i $< -o $@

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py < $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(top_srcdir)/contrib/devtools/security-check.py < $(bin_PROGRAMS)
endif

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(<D) $<

if EMBEDDED_LEVELDB
include Makefile.leveldb.include
endif

if ENABLE_TESTS
include Makefile.test.include
endif

if ENABLE_BENCH
include Makefile.bench.include
endif

if ENABLE_QT
include Makefile.qt.include
endif

if ENABLE_QT_TESTS
include Makefile.qttest.include
endif
