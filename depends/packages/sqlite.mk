package=sqlite
$(package)_version=3470000
$(package)_download_path=https://www.sqlite.org/2024/
$(package)_file_name=sqlite-autoconf-$($(package)_version).tar.gz
$(package)_sha256_hash=83eb21a6f6a649f506df8bd3aab85a08f7556ceed5dbd8dea743ea003fc3a957

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
