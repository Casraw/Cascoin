package=evmone
$(package)_version=0.18.0
$(package)_download_path=https://github.com/ipsilon/evmone.git
$(package)_dependencies=evmc intx

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_INSTALL_PREFIX=$(host_prefix)
  $(package)_config_opts+=-DCMAKE_BUILD_TYPE=Release
  $(package)_config_opts+=-DEVMONE_TESTING=OFF
  $(package)_config_opts+=-DEVMONE_FUZZING=OFF
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DHUNTER_ENABLED=OFF
  $(package)_config_opts+=-DCMAKE_PREFIX_PATH=$(host_prefix)
  $(package)_config_opts+=-Devmc_DIR=$(host_prefix)/lib/cmake/evmc
  $(package)_config_opts+=-Dintx_DIR=$(host_prefix)/lib/cmake/intx
endef

# Clone repo with submodules in fetch step, create marker file
define $(package)_fetch_cmds
  git clone --depth 1 --branch v$($(package)_version) --recursive https://github.com/ipsilon/evmone.git $($(package)_source_dir)/evmone-$($(package)_version) && \
  echo "git-clone" > $($(package)_source_dir)/evmone-$($(package)_version).marker
endef

# Move cloned repo into extract directory
define $(package)_extract_cmds
  cp -r $($(package)_source_dir)/evmone-$($(package)_version)/* . && \
  cp -r $($(package)_source_dir)/evmone-$($(package)_version)/.[!.]* . 2>/dev/null || true
endef

define $(package)_preprocess_cmds
  sed -i 's/cable_add_archive_package()/#cable_add_archive_package()/' evmc/CMakeLists.txt && \
  sed -i 's/cable_add_archive_package()/#cable_add_archive_package()/' CMakeLists.txt && \
  sed -i 's/<Windows.h>/<windows.h>/g' evmc/lib/loader/loader.c
endef

define $(package)_config_cmds
  echo "set(CMAKE_SYSTEM_NAME Windows)" > toolchain.cmake && \
  echo "set(CMAKE_C_COMPILER $(host)-gcc)" >> toolchain.cmake && \
  echo "set(CMAKE_CXX_COMPILER $(host)-g++)" >> toolchain.cmake && \
  echo "set(CMAKE_RC_COMPILER $(host)-windres)" >> toolchain.cmake && \
  echo "set(CMAKE_FIND_ROOT_PATH $(host_prefix))" >> toolchain.cmake && \
  echo "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)" >> toolchain.cmake && \
  echo "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)" >> toolchain.cmake && \
  echo "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)" >> toolchain.cmake && \
  echo 'set(CMAKE_RC_CREATE_STATIC_LIBRARY "<CMAKE_AR> rc <TARGET> <LINK_FLAGS> <OBJECTS>")' >> toolchain.cmake && \
  mkdir -p build && cd build && cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake $($(package)_config_opts)
endef

define $(package)_build_cmds
  cd build && $(MAKE) -j$(JOBS)
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
