ACLOCAL_AMFLAGS = -I m4 --install
SUBDIRS = po src doc tests
EXTRA_DIST = \
	autogen.sh \
	build-aux/config.rpath \
	LICENSE \
	TODO.md \
	tools/coverage.sh \
	$(NULL)

test-coverage:
	$(srcdir)/tools/coverage.sh run

clean-local:
	$(srcdir)/tools/coverage.sh clean

checkstyle:
	$(MAKE) -C src $@

fixstyle:
	$(MAKE) -C src $@

spelling:
	$(MAKE) -C src $@

api-compat-test:
	$(MAKE) -C src $@
