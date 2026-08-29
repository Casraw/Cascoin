package=native_ccache
$(package)_version=4.10.2
$(package)_download_path=https://github.com/ccache/ccache/releases/download/v$($(package)_version)
$(package)_file_name=ccache-$($(package)_version).tar.xz
$(package)_sha256_hash=c0b85ddfc1a3e77b105ec9ada2d24aad617fa0b447c6a94d55890972810f0f5a
$(package)_build_subdir=build

define $(package)_preprocess_cmds
  mkdir -p build
endef

define $(package)_config_cmds
  cmake -DCMAKE_INSTALL_PREFIX=$(build_prefix) \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTING=OFF \
    -DENABLE_DOCUMENTATION=OFF \
    -DREDIS_STORAGE_BACKEND=OFF \
    ..
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf lib include share
endef
