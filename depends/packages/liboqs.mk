package=liboqs
$(package)_version=0.12.0
$(package)_download_path=https://github.com/open-quantum-safe/liboqs/archive/refs/tags/
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=df999915204eb1eba311d89e83d1edd3a514d5a07374745d6a9e5b2dd0d59c08

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DOQS_BUILD_ONLY_LIB=ON
  $(package)_config_opts+=-DOQS_USE_OPENSSL=OFF
  $(package)_config_opts+=-DOQS_MINIMAL_BUILD="SIG_falcon_512;SIG_dilithium_2"
  $(package)_config_opts+=-DOQS_DIST_BUILD=ON
  $(package)_config_opts+=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DOQS_PERMIT_UNSUPPORTED_ARCHITECTURE=ON
  $(package)_config_opts+=-DOQS_OPT_TARGET=generic
endef

define $(package)_preprocess_cmds
endef

# Linux native build
define $(package)_config_cmds_linux
  mkdir -p build && cd build && cmake .. $($(package)_config_opts) \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_COMPILER=$($(package)_cc) \
    -DCMAKE_CXX_COMPILER=$($(package)_cxx)
endef

# Windows cross-compile - needs toolchain file
define $(package)_config_cmds_mingw32
  echo "set(CMAKE_SYSTEM_NAME Windows)" > toolchain.cmake && \
  echo "set(CMAKE_C_COMPILER $(host)-gcc)" >> toolchain.cmake && \
  echo "set(CMAKE_CXX_COMPILER $(host)-g++)" >> toolchain.cmake && \
  echo "set(CMAKE_RC_COMPILER $(host)-windres)" >> toolchain.cmake && \
  echo "set(CMAKE_AR $(host)-ar)" >> toolchain.cmake && \
  echo "set(CMAKE_RANLIB $(host)-ranlib)" >> toolchain.cmake && \
  echo "set(CMAKE_FIND_ROOT_PATH $(host_prefix))" >> toolchain.cmake && \
  echo "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)" >> toolchain.cmake && \
  echo "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)" >> toolchain.cmake && \
  echo "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)" >> toolchain.cmake && \
  mkdir -p build && cd build && cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake $($(package)_config_opts)
endef

# Darwin cross-compile
define $(package)_config_cmds_darwin
  mkdir -p build && cd build && cmake .. $($(package)_config_opts) \
    -DCMAKE_OSX_ARCHITECTURES=$(host_arch) \
    -DCMAKE_C_COMPILER=$($(package)_cc) \
    -DCMAKE_CXX_COMPILER=$($(package)_cxx)
endef

define $(package)_config_cmds
  $($(package)_config_cmds_$(host_os))
endef

define $(package)_build_cmds
  cd build && $(MAKE) -j$(JOBS)
endef

define $(package)_stage_cmds
  cd build && $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf share bin
endef
