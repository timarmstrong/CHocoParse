ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = autogen.sh README.md

# Include all code in library
lib_LTLIBRARIES = lib/libtsconfig.la
lib_libtsconfig_la_SOURCES = src/tsconfig.c src/tsconfig_lex.c

bin_PROGRAMS = bin/tsconfig_test
bin_tsconfig_test_SOURCES = src/tsconfig_test.c
bin_tsconfig_test_LDADD = lib/libtsconfig.la
