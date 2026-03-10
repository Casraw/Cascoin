package=openssl
$(package)_version=3.5.0
$(package)_download_path=https://github.com/openssl/openssl/releases/download/openssl-$($(package)_version)
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=344d0a79f1a9b08029b0744e2cc401a43f9c90acd1044d09a530b4885a8e9fc0

define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)" RC="$($(package)_rc)"
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix)/etc/openssl
$(package)_config_opts+=--libdir=lib
$(package)_config_opts+=no-camellia
$(package)_config_opts+=no-cast
$(package)_config_opts+=no-comp
$(package)_config_opts+=no-dso
$(package)_config_opts+=no-dtls
$(package)_config_opts+=no-ec_nistp_64_gcc_128
$(package)_config_opts+=no-gost
$(package)_config_opts+=no-idea
$(package)_config_opts+=no-md2
$(package)_config_opts+=no-mdc2
$(package)_config_opts+=no-rc4
$(package)_config_opts+=no-rc5
$(package)_config_opts+=no-rfc3779
$(package)_config_opts+=no-sctp
$(package)_config_opts+=no-seed
$(package)_config_opts+=no-shared
$(package)_config_opts+=no-ssl3
$(package)_config_opts+=no-tests
$(package)_config_opts+=no-weak-ssl-ciphers
$(package)_config_opts+=no-whirlpool
$(package)_config_opts+=no-zlib
$(package)_config_opts+=no-zlib-dynamic
$(package)_config_opts_linux=-fPIC -Wa,--noexecstack linux-x86_64
$(package)_config_opts_x86_64_linux=
$(package)_config_opts_i686_linux=linux-generic32
$(package)_config_opts_arm_linux=linux-armv4
$(package)_config_opts_aarch64_linux=linux-aarch64
$(package)_config_opts_mipsel_linux=linux-mips32
$(package)_config_opts_mips_linux=linux-mips32
$(package)_config_opts_powerpc_linux=linux-ppc
$(package)_config_opts_riscv32_linux=linux-generic32
$(package)_config_opts_riscv64_linux=linux-generic64
$(package)_config_opts_darwin=darwin64-x86_64-cc
$(package)_config_opts_x86_64_darwin=
$(package)_config_opts_arm64_darwin=darwin64-arm64-cc
$(package)_config_opts_mingw32=mingw64
$(package)_config_opts_x86_64_mingw32=
$(package)_config_opts_i686_mingw32=mingw
endef

define $(package)_preprocess_cmds
  sed -i.old 's|"engines", "apps", "test", "util", "tools", "fuzz"|"engines", "util", "tools"|' Configure
endef

define $(package)_config_cmds
  $($(package)_config_env) ./Configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) -j$(JOBS) build_libs
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_dev
endef

define $(package)_postprocess_cmds
  rm -rf share bin etc
endef
