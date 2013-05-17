SUBDIRS = tools po data tp-account-widgets

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

DISTCHECK_CONFIGURE_FLAGS =		\
	--disable-scrollkeeper		\
	--disable-schemas-install

EXTRA_DIST = 			\
        CONTRIBUTORS \
        COPYING.LGPL \
        README \
        autogen.sh \
        $(NULL)

# Workaround broken scrollkeeper that doesn't remove its files on
# uninstall.
distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper'
