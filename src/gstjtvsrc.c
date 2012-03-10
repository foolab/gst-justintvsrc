/*
 * Copyright (C) 2012 Mohammed Sameer <msameer@foolab.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstjtvsrc.h"
#include <libsoup/soup.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <librtmp/log.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

GST_DEBUG_CATEGORY_STATIC (gst_jtv_src_debug);
#define GST_CAT_DEFAULT gst_jtv_src_debug

#define XML_URL "http://usher.justin.tv/find/%s.xml?type=any"
#define SWF_URL "http://www-cdn.justin.tv/widgets/live_site_player.swf"

// Stolen from rtmpdump
#define STR2AVAL(av,str)        av.av_val = str; av.av_len = strlen(av.av_val)

typedef struct {
  char *token;
  char *connect;
  char *play;
} stream_node;

enum {
  ARG_0,
  ARG_URI,
  ARG_LOG_LEVEL,
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

extern RTMP_LogLevel RTMP_debuglevel;

static void gst_jtv_src_uri_handler_init (gpointer g_iface, gpointer iface_data);
static void gst_jtv_src_finalize (GObject * object);
static gboolean gst_jtv_src_start (GstBaseSrc * basesrc);
static gboolean gst_jtv_src_stop (GstBaseSrc * basesrc);
static GstFlowReturn gst_jtv_src_create (GstBaseSrc * basesrc, guint64 offset,
					 guint length, GstBuffer ** buffer);
static void gst_jtv_src_set_property (GObject * object, guint prop_id,
				      const GValue * value, GParamSpec * pspec);
static void gst_jtv_src_get_property (GObject * object, guint prop_id,
				      GValue * value, GParamSpec * pspec);

static void gst_jtv_src_do_init(GType type);
static gboolean gst_jtv_src_set_uri (GstJtvSrc * src, const gchar * uri);

static gboolean _gst_jtv_src_start(gpointer data);

static SoupMessage *download_xml(const gchar *channel_id);

static stream_node *get_stream_node(const char *xml, int len);

static gboolean rtmp_connect(GstJtvSrc *src, stream_node *node);

GST_BOILERPLATE_FULL (GstJtvSrc, gst_jtv_src, GstBaseSrc,
		      GST_TYPE_BASE_SRC, gst_jtv_src_do_init);

static gboolean
gst_jtv_src_is_seekable(GstBaseSrc *src) {
  return FALSE;
}

static GstURIType
gst_jtv_src_uri_get_type() {
  return GST_URI_SRC;
}

static gchar **
gst_jtv_src_uri_get_protocols () {
  static gchar *protocols[] = { (char *) "jtv", NULL };

  return protocols;
}

static const gchar *
gst_jtv_src_uri_get_uri (GstURIHandler * handler) {
  GstJtvSrc *src = GST_JTVSRC(handler);

  return src->uri;
}

static gboolean
gst_jtv_src_uri_set_uri (GstURIHandler * handler, const gchar * uri) {
  GstJtvSrc *src = GST_JTVSRC(handler);

  return gst_jtv_src_set_uri(src, uri);
}

static gboolean gst_jtv_src_set_uri (GstJtvSrc * src, const gchar * uri) {
  GST_OBJECT_LOCK(src);

  GstState state = GST_STATE(src);

  GST_OBJECT_UNLOCK(src);

  if (state != GST_STATE_NULL && state != GST_STATE_READY) {
    return FALSE;
  }

  char *channel = NULL;

  if (sscanf(uri, "jtv://%ms", &channel) != 1) {
    GST_ERROR_OBJECT (src, "Failed to parse URI %s", uri);
    return FALSE;
  }

  if (src->uri) {
    g_free(src->uri);
  }

  src->uri = g_strdup(uri);

  if (src->channel) {
    g_free(src->channel);
  }

  src->channel = channel;

  g_object_notify (G_OBJECT (src), "uri");

  gst_uri_handler_new_uri (GST_URI_HANDLER (src), src->uri);

  return TRUE;
}

static void
gst_jtv_src_do_init(GType type) {
  static const GInterfaceInfo urihandler_info = {
    gst_jtv_src_uri_handler_init,
    NULL,
    NULL};

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER,
			       &urihandler_info);
}

static void
gst_jtv_src_uri_handler_init (gpointer g_iface, gpointer iface_data) {
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_jtv_src_uri_get_type;
  iface->get_protocols = gst_jtv_src_uri_get_protocols;
  iface->get_uri = gst_jtv_src_uri_get_uri;
  iface->set_uri = gst_jtv_src_uri_set_uri;
}

static void
gst_jtv_src_base_init (gpointer gclass) {
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
				       "JtvSrc",
				       "Justin TV Source",
				       "Source",
				       "Mohammed Sameer <msameer@foolab.org>>");

  gst_element_class_add_pad_template (element_class,
				      gst_static_pad_template_get (&src_factory));
}

static void
gst_jtv_src_class_init (GstJtvSrcClass * klass) {
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *)klass;

  gobject_class->finalize = gst_jtv_src_finalize;
  gobject_class->get_property = gst_jtv_src_get_property;
  gobject_class->set_property = gst_jtv_src_set_property;
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR(gst_jtv_src_is_seekable);
  gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_jtv_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_jtv_src_stop);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR(gst_jtv_src_create);

  g_object_class_install_property (gobject_class, ARG_URI,
				   g_param_spec_string ("uri", "Stream URI",
							"URI of the stream", NULL,
							G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, ARG_LOG_LEVEL,
				   g_param_spec_int ("log-level", "Log level",
						     "librtmp log level", RTMP_LOGCRIT,
						     RTMP_LOGALL, RTMP_LOGERROR,
						     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_jtv_src_init (GstJtvSrc * src, GstJtvSrcClass * gclass) {
  gst_base_src_set_live(GST_BASE_SRC(src), TRUE);

  src->channel = NULL;
  src->uri = NULL;
  src->rtmp = NULL;
}

static gboolean
jtvsrc_init (GstPlugin * jtvsrc) {
  GST_DEBUG_CATEGORY_INIT (gst_jtv_src_debug, "jtvsrc",
      0, "Justin TV Source");

  return gst_element_register (jtvsrc, "jtvsrc", GST_RANK_NONE,
			       GST_TYPE_JTVSRC);
}

static void
gst_jtv_src_finalize (GObject * object) {
  GstJtvSrc *src = GST_JTVSRC(object);

  g_free(src->channel);
  src->channel = NULL;

  g_free(src->uri);
  src->uri = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_jtv_src_start (GstBaseSrc * basesrc) {
  GstJtvSrc *src = GST_JTVSRC(basesrc);

  if (!src->uri || !src->uri[0]) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL), (NULL));
    return FALSE;
  }

  if (!_gst_jtv_src_start(src)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_jtv_src_stop (GstBaseSrc * basesrc) {
  GstJtvSrc *src = GST_JTVSRC(basesrc);

  if (src->rtmp) {
    RTMP_Close(src->rtmp);
    RTMP_Free(src->rtmp);
    src->rtmp = NULL;
  }

  return TRUE;
}

static GstFlowReturn gst_jtv_src_create (GstBaseSrc * basesrc, guint64 offset,
					 guint length, GstBuffer ** buffer) {

  GstJtvSrc *src = GST_JTVSRC(basesrc);

  if (!src) {
    return GST_FLOW_ERROR;
  }

  GstBuffer *buf = gst_buffer_new_and_alloc(length);

  guint8 *data = GST_BUFFER_DATA(buf);
  int size = GST_BUFFER_SIZE(buf);

  int read = RTMP_Read(src->rtmp, data, size);

  if (read == 0) {
    gst_buffer_unref(buf);
    return GST_FLOW_UNEXPECTED;
  }
  else if (read < 0) {
    // TODO: error
    gst_buffer_unref(buf);
    return GST_FLOW_ERROR;
  }

  GST_BUFFER_SIZE(buf) = read;

  *buffer = buf;

  return GST_FLOW_OK;
  //  while (1) {
  //    sleep (1);
  //  }

  // TODO:
#if 0


  if (src->read_position != offset) {
    if (!src->file->seek(offset)) {
      return GST_FLOW_ERROR;
    }
  }

  GstBuffer *buf = gst_buffer_new_and_alloc(length);

  qint64 read = src->file->read((char *)GST_BUFFER_DATA(buf), length);

  if (read == -1) {
    gst_buffer_unref(buf);
    return GST_FLOW_ERROR;
  }
  else if (read == 0) {
    gst_buffer_unref(buf);
    return GST_FLOW_UNEXPECTED;
  }

  *buffer = buf;

  GST_BUFFER_SIZE(buf) = read;

  GST_BUFFER_OFFSET(buf) = offset;
  GST_BUFFER_OFFSET_END(buf) = offset + read;

  src->read_position += read;

  return GST_FLOW_OK;

#endif

  return GST_FLOW_ERROR;
}

static void
gst_jtv_src_set_property (GObject * object, guint prop_id,
			  const GValue * value, GParamSpec * pspec) {
  GstJtvSrc *src;

  g_return_if_fail (GST_IS_JTVSRC (object));

  src = GST_JTVSRC (object);

  switch (prop_id) {
  case ARG_URI:
    if (!gst_jtv_src_set_uri(src, g_value_get_string (value))) {
      g_warning ("Failed to set 'uri'");
    }

    break;
  case ARG_LOG_LEVEL:
    RTMP_debuglevel = g_value_get_int(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_jtv_src_get_property (GObject * object, guint prop_id, GValue * value,
			  GParamSpec * pspec) {
  GstJtvSrc *src;

  g_return_if_fail (GST_IS_JTVSRC (object));

  src = GST_JTVSRC (object);

  switch (prop_id) {
  case ARG_URI:
    g_value_set_string (value, src->uri);
    break;
  case ARG_LOG_LEVEL:
    g_value_set_int(value, RTMP_debuglevel);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static SoupMessage *download_xml(const gchar *channel_id) {
  SoupSession *session = soup_session_sync_new();

  char *url = g_strdup_printf(XML_URL, channel_id);

  SoupMessage *msg = soup_message_new ("GET", url);
  g_free(url);

  guint status = soup_session_send_message (session, msg);

  if (!SOUP_STATUS_IS_SUCCESSFUL(status)) {

    g_object_unref(msg);

    g_object_unref(session);

    // TODO: error

    return NULL;
  }

  g_object_unref(session);

  return msg;
}

static stream_node *get_stream_node(const char *xml, int len) {
  xmlNodePtr cur = NULL;

  xmlDocPtr doc = xmlParseMemory(xml, len);
  if (!doc) {
    // TODO: error
    return NULL;
  }

  cur = xmlDocGetRootElement(doc);
  if (!cur) {
    // TODO: error
    xmlFreeDoc(doc);
    return NULL;
  }

  if (xmlStrcmp(cur->name, (const xmlChar *) "nodes")) {
    // TODO: error
    xmlFreeDoc(doc);
    return NULL;
  }

  cur = cur->xmlChildrenNode;
  if (!cur) {
    // TODO: error
    xmlFreeDoc(doc);
    return NULL;
  }

  //  puts(cur->name);

  stream_node *node = g_new(stream_node, sizeof(stream_node));
  if (!node) {
    // TODO: error
    xmlFreeDoc(doc);
    return NULL;
  }

  cur = cur->xmlChildrenNode;
  while (cur) {
    if (!xmlStrcmp(cur->name, (const xmlChar *)"token")) {
      xmlChar *key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
      node->token = g_strdup(key);
      xmlFree(key);
    }
    else if (!xmlStrcmp(cur->name, (const xmlChar *)"connect")) {
      xmlChar *key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
      node->connect = g_strdup(key);
      xmlFree(key);
    }
    else if (!xmlStrcmp(cur->name, (const xmlChar *)"play")) {
      xmlChar *key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
      node->play = g_strdup(key);
      xmlFree(key);
    }
    cur = cur->next;
  }

  if (!node->play || !node->connect || !node->token) {
    // TODO: error.

    g_free(node->play);
    g_free(node->connect);
    g_free(node->token);
    g_free(node);
    node = NULL;
  }

  xmlFreeDoc(doc);

  return node;
}

static gboolean rtmp_connect(GstJtvSrc *src, stream_node *node) {
#if 0
  if (!RTMP_ParseURL(uri, &protocol, &host, &port, &path, &app)) {
    // TODO: error

    g_free(uri);

    return FALSE;
  }
#endif

  src->rtmp = RTMP_Alloc();
  RTMP_Init(src->rtmp);

  char *uri = g_strdup_printf("%s/%s", node->connect, node->play);

  if (!RTMP_SetupURL(src->rtmp, uri)) {
    // TODO: error.
    g_free(uri);
    RTMP_Free(src->rtmp);
    src->rtmp = NULL;
    return FALSE;
  }

  AVal swfopt; STR2AVAL(swfopt, "swfUrl");
  AVal jtvopt; STR2AVAL(jtvopt, "jtv");
  AVal swf; STR2AVAL(swf, SWF_URL);
  AVal jtv; STR2AVAL(jtv, node->token);

  if (!RTMP_SetOpt(src->rtmp, &swfopt, &swf) || !RTMP_SetOpt(src->rtmp, &jtvopt, &jtv)) {
    // TODO: error

    RTMP_Free(src->rtmp);
    src->rtmp = NULL;

    g_free(uri);

    return FALSE;
  }


  if (!RTMP_Connect(src->rtmp, NULL)) {
    // TODO: error.
    RTMP_Free(src->rtmp);
    src->rtmp = NULL;
    g_free(uri);
    return FALSE;
  }

  if (!RTMP_ConnectStream(src->rtmp, 0)) {
    // TODO: error.
    RTMP_Free(src->rtmp);
    src->rtmp = NULL;
    g_free(uri);
    return FALSE;
  }

  g_free(uri);

  return TRUE;
}

gboolean _gst_jtv_src_start(gpointer data) {
  GstJtvSrc *src = (GstJtvSrc *)data;

  SoupMessage *msg = download_xml(src->channel);
  if (!msg) {
    return FALSE;
  }

  stream_node *node = get_stream_node(msg->response_body->data, msg->response_body->length);

  g_object_unref(msg);

  if (!node) {
    return FALSE;
  }

  puts(node->play);
  puts(node->token);
  puts(node->connect);

  if (!rtmp_connect(src, node)) {
    g_free(node->play);
    g_free(node->connect);
    g_free(node->token);
    g_free(node);

    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, "jtvsrc",
		  "Justin TV Source", jtvsrc_init, VERSION,
		  "LGPL", PACKAGE,
		  "https://gitorious.org/justin-tv-gstreamer-source")
