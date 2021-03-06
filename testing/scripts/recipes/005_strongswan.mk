#!/usr/bin/make

PV  = $(SWANVERSION)
PKG = strongswan-$(PV)
TAR = $(PKG).tar.bz2
SRC = http://download.strongswan.org/$(TAR)

NUM_CPUS := $(shell getconf _NPROCESSORS_ONLN)

CONFIG_OPTS = \
	--sysconfdir=/etc \
	--with-random-device=/dev/urandom \
	--disable-load-warning \
	--enable-curl \
	--enable-ldap \
	--enable-eap-aka \
	--enable-eap-aka-3gpp2 \
	--enable-eap-sim \
	--enable-eap-sim-file \
	--enable-eap-md5 \
	--enable-md4 \
	--enable-eap-mschapv2 \
	--enable-eap-identity \
	--enable-eap-radius \
	--enable-eap-dynamic \
	--enable-eap-tls \
	--enable-eap-ttls \
	--enable-eap-peap \
	--enable-eap-tnc \
	--enable-tnc-pdp \
	--enable-tnc-imc \
	--enable-tnc-imv \
	--enable-tnccs-11 \
	--enable-tnccs-20 \
	--enable-tnccs-dynamic \
	--enable-imc-test \
	--enable-imv-test \
	--enable-imc-scanner \
	--enable-imv-scanner \
	--enable-imc-os \
	--enable-imv-os \
	--enable-imc-attestation \
	--enable-imv-attestation \
	--enable-sql \
	--enable-sqlite \
	--enable-mediation \
	--enable-openssl \
	--enable-blowfish \
	--enable-kernel-pfkey \
	--enable-integrity-test \
	--enable-leak-detective \
	--enable-load-tester \
	--enable-test-vectors \
	--enable-gcrypt \
	--enable-socket-default \
	--enable-socket-dynamic \
	--enable-dhcp \
	--enable-farp \
	--enable-addrblock \
	--enable-ctr \
	--enable-ccm \
	--enable-gcm \
	--enable-cmac \
	--enable-ha \
	--enable-af-alg \
	--enable-whitelist \
	--enable-xauth-generic \
	--enable-xauth-eap \
	--enable-pkcs8 \
	--enable-unity

all: install

$(TAR):
	wget $(SRC)

$(PKG): $(TAR)
	tar xfj $(TAR)

configure: $(PKG)
	cd $(PKG) && ./configure $(CONFIG_OPTS)

build: configure
	cd $(PKG) && make -j $(NUM_CPUS)

install: build
	cd $(PKG) && make install
