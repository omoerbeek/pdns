SUBDIRS = ext modules pdns codedocs docs

EXTRA_DIST = \
	INSTALL \
	NOTICE \
	README \
	COPYING \
	codedocs/doxygen.conf \
	contrib/powerdns.solaris.init.d \
	pdns/named.conf.parsertest \
	regression-tests/zones/unit.test \
	regression-tests/zones/unit2.test \
	builder-support/gen-version

ACLOCAL_AMFLAGS = -I m4

dvi: # do nothing to build dvi

format-code:
	./build-scripts/format-code `find . -type f -name '*.[ch][ch]' | LANG=C sort | LANG=C comm -23 - .not-formatted`

