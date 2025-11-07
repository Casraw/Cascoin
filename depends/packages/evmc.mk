package=evmc
$(package)_version=12.1.0
$(package)_download_path=https://github.com/ethereum/evmc/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=0d5458015bf38a5358fad04cc290d21ec40122d1eb6420e0b33ae25546984bcd
$(package)_dependencies=

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DEVMC_TESTING=OFF
  $(package)_config_opts+=-DEVMC_TOOLS=OFF
  $(package)_config_opts+=-DEVMC_EXAMPLES=OFF
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts_release=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts_debug=-DCMAKE_BUILD_TYPE=Debug
endef

define $(package)_config_cmds
  $($(package)_cmake) .
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf share
endef