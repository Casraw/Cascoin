package=liboqs
$(package)_version=0.10.1
$(package)_download_path=https://github.com/open-quantum-safe/liboqs/archive/refs/tags/
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=00ca8aba65cd8c8eac00ddf978f4cac9dd23bb039f357448b60b7e3eed8f02da

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DOQS_BUILD_ONLY_LIB=ON
  $(package)_config_opts+=-DOQS_USE_OPENSSL=OFF
  $(package)_config_opts+=-DOQS_MINIMAL_BUILD="OQS_ENABLE_SIG_falcon_512"
  $(package)_config_opts+=-DOQS_DIST_BUILD=ON
  $(package)_config_opts+=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts_linux=-DCMAKE_POSITION_INDEPENDENT_CODE=ON
  $(package)_config_opts_darwin=-DCMAKE_OSX_ARCHITECTURES=$(host_arch)
  $(package)_config_opts_mingw32=-DCMAKE_SYSTEM_NAME=Windows
  $(package)_config_opts_mingw32+=-DCMAKE_CROSSCOMPILING=ON
endef

define $(package)_preprocess_cmds
endef

define $(package)_config_cmds
  mkdir -p build && \
  cd build && \
  cmake $($(package)_config_opts) ..
endef

define $(package)_build_cmds
  cd build && \
  $(MAKE) -j$(JOBS)
endef

define $(package)_stage_cmds
  cd build && \
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf share bin
endef
