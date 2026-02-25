package=expat
$(package)_version=2.6.4
$(package)_download_path=https://github.com/libexpat/libexpat/releases/download/R_2_6_4
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=a695629dae047055b37d50a0ff4776d1d45d0a4c842cf4ccee158441f55ff7ee

define $(package)_set_vars
$(package)_config_opts=--disable-static
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
