# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstjtv.la

# sources used to compile this plug-in
libgstjtv_la_SOURCES = gstjtvsrc.c gstjtvsrc.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstjtv_la_CFLAGS = $(GST_CFLAGS) $(SOUP_CFLAGS) $(RTMP_CFLAGS)
libgstjtv_la_LIBADD = $(GST_LIBS) $(SOUP_LIBS) $(RTMP_LIBS)
libgstjtv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjtv_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstjtvsrc.h
