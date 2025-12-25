package=evmc
$(package)_version=12.1.0
$(package)_download_path=https://github.com/ethereum/evmc/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=0d5458015bf38a5358fad04cc290d21ec40122d1eb6420e0b33ae25546984bcd
$(package)_dependencies=

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DEVMC_TESTING=OFF
  $(package)_config_opts+=-DEVMC_TOOLS=OFF
  $(package)_config_opts+=-DEVMC_EXAMPLES=OFF
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
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
  $(package)_config_opts_darwin+=-DCMAKE_C_COMPILER=$(host)-clang
  $(package)_config_opts_darwin+=-DCMAKE_CXX_COMPILER=$(host)-clang++
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
  echo "prefix=$(host_prefix)" > lib/pkgconfig/evmc.pc && \
  echo "exec_prefix=\$${prefix}" >> lib/pkgconfig/evmc.pc && \
  echo "libdir=\$${exec_prefix}/lib" >> lib/pkgconfig/evmc.pc && \
  echo "includedir=\$${prefix}/include" >> lib/pkgconfig/evmc.pc && \
  echo "" >> lib/pkgconfig/evmc.pc && \
  echo "Name: evmc" >> lib/pkgconfig/evmc.pc && \
  echo "Description: Ethereum Virtual Machine C API" >> lib/pkgconfig/evmc.pc && \
  echo "Version: $($(package)_version)" >> lib/pkgconfig/evmc.pc && \
  echo "Libs: -L\$${libdir} -levmc-loader -levmc-instructions" >> lib/pkgconfig/evmc.pc && \
  echo "Cflags: -I\$${includedir}" >> lib/pkgconfig/evmc.pc
endef