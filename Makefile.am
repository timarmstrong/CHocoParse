ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = autogen.sh README.md

# Include all code in library
lib_LTLIBRARIES = libtsconfig.la
libtsconfig_la_SOURCES=src/tsconfig.c

bin_PROGRAMS = bin/tsconfig_test
bin_tsconfig_test_SOURCES = src/tsconfig_test.c
bin_tsconfig_test_LDADD =  libtsconfig.la
