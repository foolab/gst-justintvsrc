#ifndef __GST_JTVSRC_H__
#define __GST_JTVSRC_H__

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <librtmp/rtmp.h>

G_BEGIN_DECLS

#define GST_TYPE_JTVSRC \
  (gst_jtv_src_get_type())
#define GST_JTVSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JTVSRC,GstJtvSrc))
#define GST_JTVSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_JTVSRC,GstJtvSrcClass))
#define GST_IS_JTVSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JTVSRC))
#define GST_IS_JTVSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JTVSRC))

typedef struct _GstJtvSrc      GstJtvSrc;
typedef struct _GstJtvSrcClass GstJtvSrcClass;

struct _GstJtvSrc
{
  GstBaseSrc element;

  gchar *channel;
  gchar *uri;
  RTMP *rtmp;

  gint64 offset;
};

struct _GstJtvSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_jtv_src_get_type (void);

G_END_DECLS

#endif /* __GST_JTVSRC_H__ */
