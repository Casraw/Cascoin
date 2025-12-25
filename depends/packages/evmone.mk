package=evmone
$(package)_version=0.18.0
$(package)_download_path=https://github.com/ethereum/evmone/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=e9f1f43c82d8b5675b7e8b2c0ce7ffba808508475ebf59c9dc9700c3379dcd93
$(package)_dependencies=evmc

# Additional source for evmc submodule
$(package)_extra_sources=evmc-12.1.0.tar.gz

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),https://github.com/ethereum/evmc/archive/refs/tags/,v12.1.0.tar.gz,evmc-12.1.0.tar.gz,0d5458015bf38a5358fad04cc290d21ec40122d1eb6420e0b33ae25546984bcd)
endef

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DEVMONE_TESTING=OFF
  $(package)_config_opts+=-DEVMONE_FUZZING=OFF
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DHUNTER_ENABLED=OFF
  $(package)_config_opts+=-DCMAKE_PREFIX_PATH=$(host_prefix)
  $(package)_config_opts+=-Devmc_DIR=$(host_prefix)/lib/cmake/evmc
  # Cross-compilation settings for MinGW
  $(package)_config_opts_mingw32=-DCMAKE_SYSTEM_NAME=Windows
  $(package)_config_opts_mingw32+=-DCMAKE_C_COMPILER=$(host)-gcc
  $(package)_config_opts_mingw32+=-DCMAKE_CXX_COMPILER=$(host)-g++
  $(package)_config_opts_mingw32+=-DCMAKE_RC_COMPILER=$(host)-windres
  $(package)_config_opts_mingw32+=-DCMAKE_FIND_ROOT_PATH=$(host_prefix)
  $(package)_config_opts_mingw32+=-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
  $(package)_config_opts_mingw32+=-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
  $(package)_config_opts_mingw32+=-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
  # Darwin cross-compilation
  $(package)_config_opts_darwin=-DCMAKE_SYSTEM_NAME=Darwin
endef

define $(package)_preprocess_cmds
  rm -rf evmc && \
  mkdir -p evmc && \
  tar --strip-components=1 -xf $($(package)_source_dir)/evmc-12.1.0.tar.gz -C evmc
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
  rm -rf share && \
  mkdir -p lib/pkgconfig && \
  echo "prefix=$(host_prefix)" > lib/pkgconfig/evmone.pc && \
  echo "exec_prefix=\$${prefix}" >> lib/pkgconfig/evmone.pc && \
  echo "libdir=\$${exec_prefix}/lib" >> lib/pkgconfig/evmone.pc && \
  echo "includedir=\$${prefix}/include" >> lib/pkgconfig/evmone.pc && \
  echo "" >> lib/pkgconfig/evmone.pc && \
  echo "Name: evmone" >> lib/pkgconfig/evmone.pc && \
  echo "Description: Fast Ethereum Virtual Machine implementation" >> lib/pkgconfig/evmone.pc && \
  echo "Version: $($(package)_version)" >> lib/pkgconfig/evmone.pc && \
  echo "Requires: evmc" >> lib/pkgconfig/evmone.pc && \
  echo "Libs: -L\$${libdir} -levmone" >> lib/pkgconfig/evmone.pc && \
  echo "Cflags: -I\$${includedir}" >> lib/pkgconfig/evmone.pc
endef
