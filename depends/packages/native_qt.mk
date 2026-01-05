package=native_qt
include packages/qt_details.mk

$(package)_version=$(qt_details_version)
$(package)_download_path=$(qt_details_download_path)
$(package)_file_name=$(qt_details_qtbase_file_name)
$(package)_sha256_hash=$(qt_details_qtbase_sha256_hash)

# Minimal host Qt used for cross-compiling Qt: provides Qt6HostInfo and host tools.

define $(package)_set_vars
$(package)_config_opts := -no-egl -no-eglfs -no-evdev -no-glib -no-icu -no-kms -no-linuxfb -no-libjpeg -no-libproxy -no-libudev -no-mtdev -no-opengl -no-openssl -no-openvg -no-reduce-relocations -no-system-proxies -no-use-gold-linker -no-zstd
$(package)_config_opts += -nomake examples -nomake tests
$(package)_config_opts += -prefix $(build_prefix)
$(package)_config_opts += -qt-doubleconversion -qt-harfbuzz -qt-libpng -qt-pcre -qt-zlib
$(package)_config_opts += -no-feature-printsupport -no-feature-networkproxy -no-feature-sql -no-feature-vulkan -no-feature-concurrent -no-feature-gssapi -no-feature-http
$(package)_config_opts += -no-dbus -no-freetype -no-pkg-config

# CMake options kept minimal; rely on defaults for host build
$(package)_cmake_opts := -DCMAKE_PREFIX_PATH=$(build_prefix)
$(package)_cmake_opts += -DQT_GENERATE_SBOM=OFF
endef

$(package)_top_download_path=$(qt_details_top_download_path)
$(package)_top_cmakelists_file_name=$(qt_details_top_cmakelists_file_name)
$(package)_top_cmakelists_download_file=$(qt_details_top_cmakelists_download_file)
$(package)_top_cmakelists_sha256_hash=$(qt_details_top_cmakelists_sha256_hash)
$(package)_top_cmake_download_path=$(qt_details_top_cmake_download_path)
$(package)_top_cmake_ecmoptionaladdsubdirectory_file_name=$(qt_details_top_cmake_ecmoptionaladdsubdirectory_file_name)
$(package)_top_cmake_ecmoptionaladdsubdirectory_download_file=$(qt_details_top_cmake_ecmoptionaladdsubdirectory_download_file)
$(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash=$(qt_details_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)
$(package)_top_cmake_qttoplevelhelpers_file_name=$(qt_details_top_cmake_qttoplevelhelpers_file_name)
$(package)_top_cmake_qttoplevelhelpers_download_file=$(qt_details_top_cmake_qttoplevelhelpers_download_file)
$(package)_top_cmake_qttoplevelhelpers_sha256_hash=$(qt_details_top_cmake_qttoplevelhelpers_sha256_hash)

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_download_path),$($(package)_top_cmakelists_download_file),$($(package)_top_cmakelists_file_name)-$($(package)_version),$($(package)_top_cmakelists_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_cmake_download_path),$($(package)_top_cmake_ecmoptionaladdsubdirectory_download_file),$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version),$($(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_cmake_download_path),$($(package)_top_cmake_qttoplevelhelpers_download_file),$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version),$($(package)_top_cmake_qttoplevelhelpers_sha256_hash))
endef

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmakelists_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmakelists_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmake_qttoplevelhelpers_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir qtbase && \
  $(build_TAR) --no-same-owner --strip-components=1 -xf $($(package)_source) -C qtbase && \
  cp $($(package)_source_dir)/$($(package)_top_cmakelists_file_name)-$($(package)_version) ./$($(package)_top_cmakelists_file_name) && \
  mkdir cmake && \
  cp $($(package)_source_dir)/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version) cmake/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name) && \
  cp $($(package)_source_dir)/$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version) cmake/$($(package)_top_cmake_qttoplevelhelpers_file_name)
endef

define $(package)_config_cmds
  cd qtbase && \
  ./configure -top-level $($(package)_config_opts) -- $($(package)_cmake_opts) $(if $(shell which $(build_NINJA) 2>/dev/null),-G Ninja,)
endef

define $(package)_build_cmds
  cmake --build . -- $$(filter -j%,$$(MAKEFLAGS))
endef

define $(package)_stage_cmds
  cmake --install . --prefix $($(package)_staging_prefix_dir)
endef

define $(package)_postprocess_cmds
  mkdir -p bin && \
  cd bin && \
  for tool in moc rcc uic; do \
    if [ -f ../libexec/"$$$$tool" ] && [ ! -e "$$$$tool" ]; then \
      ln -sf ../libexec/"$$$$tool" "$$$$tool"; \
    fi; \
  done
endef
