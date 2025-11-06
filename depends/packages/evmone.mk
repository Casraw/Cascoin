package=evmone
$(package)_version=0.11.0
$(package)_download_path=https://github.com/ethereum/evmone/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=b8c6b4b8c8b4b8c8b4b8c8b4b8c8b4b8c8b4b8c8b4b8c8b4b8c8b4b8c8b4b8c
$(package)_dependencies=evmc

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DEVMONE_TESTING=OFF
  $(package)_config_opts+=-DEVMONE_FUZZING=OFF
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