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
  xmlChar *token;
  xmlChar *connect;
  xmlChar *play;
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

static SoupMessage *download_xml(GstJtvSrc *src);

static stream_node *get_stream_node(GstJtvSrc *src, const char *xml, int len);

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
  src->rtmp_url = NULL;
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

  SoupMessage *msg = NULL;
  stream_node *node = NULL;
  gboolean ret_val = TRUE;

  if (!src->uri || !src->uri[0]) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL), ("No uri given"));
    return FALSE;
  }

  if (!src->channel || !src->channel[0]) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL), ("No channel given"));
    return FALSE;
  }

  msg = download_xml(src);
  if (!msg) {
    ret_val = FALSE;
    goto out;
  }

  node = get_stream_node(src, msg->response_body->data, msg->response_body->length);

  g_object_unref(msg);
  msg = NULL;

  if (!node) {
    ret_val = FALSE;
    goto out;
  }

  GST_DEBUG_OBJECT (src, "Using the following stream properties: Play: '%s', Token: '%s', Connect: '%s'", node->play, node->token, node->connect);

  if (!rtmp_connect(src, node)) {
    ret_val = FALSE;
    goto out;
  }

 out:
  if (msg) {
    g_object_unref(msg);
    msg = NULL;
  }

  if (node) {
    xmlFree(node->play);
    xmlFree(node->connect);
    xmlFree(node->token);
    g_free(node);
    node = NULL;
  }

  return ret_val;
}

static gboolean
gst_jtv_src_stop (GstBaseSrc * basesrc) {
  GstJtvSrc *src = GST_JTVSRC(basesrc);

  if (src->rtmp) {
    RTMP_Close(src->rtmp);
    RTMP_Free(src->rtmp);
    src->rtmp = NULL;
    g_free(src->rtmp_url);
    src->rtmp_url = NULL;
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

  int read = RTMP_Read(src->rtmp, (char *)data, size);

  if (read == 0) {
    gst_buffer_unref(buf);
    return GST_FLOW_UNEXPECTED;
  }
  else if (read < 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), ("Failed to read RTMP stream"));
    gst_buffer_unref(buf);
    return GST_FLOW_ERROR;
  }

  GST_BUFFER_SIZE(buf) = read;

  *buffer = buf;

  return GST_FLOW_OK;
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

static SoupMessage *download_xml(GstJtvSrc *src) {
  char *url = g_strdup_printf(XML_URL, src->channel);

  SoupSession *session = soup_session_sync_new();

  SoupMessage *msg = soup_message_new ("GET", url);

  guint status = soup_session_send_message (session, msg);

  if (!SOUP_STATUS_IS_SUCCESSFUL(status)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), ("Error %d while downloading %s",
							  status, url));

    g_object_unref(msg);

    msg = NULL;

    goto out;
  }

 out:
  g_free(url);
  g_object_unref(session);

  return msg;
}

static stream_node *get_stream_node(GstJtvSrc *src, const char *xml, int len) {
  xmlDocPtr doc = xmlParseMemory(xml, len);
  stream_node *node = NULL;
  xmlNodePtr cur = NULL;

  if (!doc) {
    GST_ERROR_OBJECT (src, "Failed to parse XML");
    goto out;
  }

  cur = xmlDocGetRootElement(doc);
  if (!cur) {
    GST_ERROR_OBJECT (src, "No root element found");
    goto out;
  }

  if (xmlStrcmp(cur->name, (const xmlChar *) "nodes")) {
    GST_ERROR_OBJECT (src, "Invalid XML document");
    goto out;
  }

  cur = cur->xmlChildrenNode;
  if (!cur) {
    GST_ERROR_OBJECT (src, "Failed to get stream node");
    goto out;
  }

  GST_DEBUG_OBJECT (src, "Using node %s", cur->name);

  node = g_new(stream_node, sizeof(stream_node));

  cur = cur->xmlChildrenNode;
  while (cur) {
    if (!xmlStrcmp(cur->name, (const xmlChar *)"token")) {
      node->token = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    }
    else if (!xmlStrcmp(cur->name, (const xmlChar *)"connect")) {
      node->connect = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    }
    else if (!xmlStrcmp(cur->name, (const xmlChar *)"play")) {
      node->play = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    }

    cur = cur->next;
  }

  if (!node->play || !node->connect || !node->token) {
    GST_ERROR_OBJECT (src, "Invalid stream node");

    xmlFree(node->play);
    xmlFree(node->connect);
    xmlFree(node->token);
    g_free(node);
    node = NULL;

    goto out;
  }

 out:
  if (doc) {
    xmlFreeDoc(doc);
  }

  return node;
}

static gboolean rtmp_connect(GstJtvSrc *src, stream_node *node) {
  src->rtmp = RTMP_Alloc();

  src->rtmp_url = g_strdup_printf("%s/%s", node->connect, node->play);

  RTMP_Init(src->rtmp);

  if (!RTMP_SetupURL(src->rtmp, src->rtmp_url)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
		       ("Failed to setup URL '%s'", src->rtmp_url));
    goto error;
  }

  AVal swfopt; STR2AVAL(swfopt, "swfUrl");
  AVal jtvopt; STR2AVAL(jtvopt, "jtv");
  AVal swf; STR2AVAL(swf, SWF_URL);
  AVal jtv; STR2AVAL(jtv, (char *)node->token);

  if (!RTMP_SetOpt(src->rtmp, &swfopt, &swf)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
		       ("Failed to set swf URL '%s'", SWF_URL));
    goto error;
  }

  if (!RTMP_SetOpt(src->rtmp, &jtvopt, &jtv)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
		       ("Failed to set jtv token"));

    goto error;
  }

  if (!RTMP_Connect(src->rtmp, NULL)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
		       ("Could not connect to RTMP server"));

    goto error;
  }

  if (!RTMP_ConnectStream(src->rtmp, 0)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
		       ("Could not connect to RTMP stream '%s'", src->rtmp_url));

    goto error;
  }

  return TRUE;

 error:
  if (src->rtmp) {
    RTMP_Free(src->rtmp);
    src->rtmp = NULL;
  }

  if (src->rtmp_url) {
    g_free(src->rtmp_url);
    src->rtmp_url = NULL;
  }

  return FALSE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, "jtvsrc",
		  "Justin TV Source", jtvsrc_init, VERSION,
		  "LGPL", PACKAGE,
		  "https://gitorious.org/justin-tv-gstreamer-source")
