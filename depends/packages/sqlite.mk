package=sqlite
$(package)_version=3440200
$(package)_download_path=https://www.sqlite.org/2023/
$(package)_file_name=sqlite-autoconf-$($(package)_version).tar.gz
$(package)_sha256_hash=1c6719a148bc41cf0f2bbbe3926d7ce3f5ca09d878f1246fcc20767b175bb407

define $(package)_set_vars
  $(package)_config_opts=--disable-shared --disable-readline --disable-dynamic-extensions --enable-threadsafe
  $(package)_config_opts_linux=--with-pic
  $(package)_cflags=-DSQLITE_ENABLE_COLUMN_METADATA=1
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -j$(JOBS)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
endef
