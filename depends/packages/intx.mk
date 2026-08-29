package=intx
$(package)_version=0.13.0
$(package)_download_path=https://github.com/chfast/intx/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=849577814e6feb9d4fc3f66f99698eee51dc4b7e3e035c1a2cb76e0d9c52c2e5
$(package)_dependencies=

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DINTX_TESTING=OFF
  $(package)_config_opts+=-DINTX_BENCHMARKING=OFF
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
endef

define $(package)_config_cmds
  mkdir -p build && cd build && cmake .. $($(package)_config_opts)
endef

define $(package)_build_cmds
  cd build && $(MAKE) -j$(JOBS)
endef

define $(package)_stage_cmds
  cd build && $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
