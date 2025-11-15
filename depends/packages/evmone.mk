package=evmone
$(package)_version=0.18.0
$(package)_download_path=https://github.com/ethereum/evmone/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=e9f1f43c82d8b5675b7e8b2c0ce7ffba808508475ebf59c9dc9700c3379dcd93
$(package)_dependencies=evmc

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DEVMONE_TESTING=OFF
  $(package)_config_opts+=-DEVMONE_FUZZING=OFF
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DEVMC_ROOT=$(host_prefix)
endef

define $(package)_preprocess_cmds
  rm -rf evmc && \
  git init && \
  git config user.email "build@cascoin.local" && \
  git config user.name "Cascoin Build" && \
  git add . && \
  git commit -m "Initial" && \
  git submodule add https://github.com/ethereum/evmc.git evmc && \
  cd evmc && git checkout v$(evmc_version) && cd ..
endef

define $(package)_config_cmds
  mkdir -p build && cd build && cmake .. $($(package)_config_opts)
endef

define $(package)_build_cmds
  cd build && $(MAKE)
endef

define $(package)_stage_cmds
  cd build && $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf share
endef