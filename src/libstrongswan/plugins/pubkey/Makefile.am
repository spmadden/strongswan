
INCLUDES = -I$(top_srcdir)/src/libstrongswan

AM_CFLAGS = -rdynamic

if MONOLITHIC
noinst_LTLIBRARIES = libstrongswan-pubkey.la
else
plugin_LTLIBRARIES = libstrongswan-pubkey.la
endif

libstrongswan_pubkey_la_SOURCES = \
	pubkey_plugin.h pubkey_plugin.c \
	pubkey_cert.h pubkey_cert.c

libstrongswan_pubkey_la_LDFLAGS = -module -avoid-version
