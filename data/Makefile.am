SUBDIRS = icons

gsettings_SCHEMAS = org.gnome.telepathy-account-widgets.gschema.xml

if ENABLE_SETTINGS
@GSETTINGS_RULES@

# We need to compile schemas at make time
# to run from source tree
gschemas.compiled: $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=. .

all-local: gschemas.compiled
endif

EXTRA_DIST = 				\
	$(schemas_DATA)			\
	$(gsettings_SCHEMAS)		\
	$(NULL)

DISTCLEANFILES = 		\
	$(schemas_DATA)		\
	gschemas.compiled	\
	$(NULL)
