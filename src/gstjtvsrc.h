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
  gchar *rtmp_url;
  RTMP *rtmp;
  gulong cur_offset;
};

struct _GstJtvSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_jtv_src_get_type (void);

G_END_DECLS

#endif /* __GST_JTVSRC_H__ */
