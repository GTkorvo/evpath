
AUTOMAKE_OPTIONS = no-dependencies foreign

SUBDIRS = .

INCLUDES = -I$(srcdir)/.. -I..

TESTS = evtest rawtest chain_test filter_test tree_test multi_test dchain_test

evtest_SOURCES = evtest.c test_support.c

rawtest_SOURCES = rawtest.c test_support.c

chain_test_SOURCES = chain_test.c test_support.c

filter_test_SOURCES = filter_test.c test_support.c

tree_test_SOURCES = tree_test.c test_support.c

multi_test_SOURCES = multi_test.c test_support.c

dchain_test_SOURCES = dchain_test.c test_support.c

noinst_PROGRAMS = $(TESTS) 

LDADD = ../libevpath.la 

LDFLAGS = -static
