/* GStreamer faceoverlay plugin
 * Copyright (C) 2011 Laura Lucas Alday <lauralucas@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-faceoverlay
 *
 * Overlays a SVG image over a detected face in a video stream.
 * x, y, w, and h properties are optional, and change the image position and
 * size relative to the detected face position and size.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch autovideosrc ! ffmpegcolorspace ! faceoverlay location=/path/to/gnome-video-effects/pixmaps/bow.svg x=-5 y=-15 w=0.3 h=0.1 ! ffmpegcolorspace ! autovideosink

 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstfaceoverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_face_overlay_debug);
#define GST_CAT_DEFAULT gst_face_overlay_debug

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_X,
  PROP_Y,
  PROP_W,
  PROP_H
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb; video/x-raw-yuv")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb; video/x-raw-yuv")
    );

GST_BOILERPLATE (GstFaceOverlay, gst_face_overlay, GstBin, GST_TYPE_BIN);

static void gst_face_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_face_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_face_overlay_message_handler (GstBin * bin,
    GstMessage * message);
static GstStateChangeReturn gst_face_overlay_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_face_overlay_create_children (GstFaceOverlay * filter);

static gboolean
gst_face_overlay_create_children (GstFaceOverlay * filter)
{
  GstElement *csp, *face_detect, *overlay;
  GstPad *pad;

  csp = gst_element_factory_make ("ffmpegcolorspace", NULL);
  face_detect = gst_element_factory_make ("facedetect", NULL);
  overlay = gst_element_factory_make ("rsvgoverlay", NULL);

  /* FIXME: post missing-plugin messages on NULL->READY if needed */
  if (csp == NULL || face_detect == NULL || overlay == NULL)
    goto missing_element;

  g_object_set (face_detect, "display", FALSE, NULL);

  gst_bin_add_many (GST_BIN (filter), face_detect, csp, overlay, NULL);
  filter->svg_overlay = overlay;

  if (!gst_element_link_many (face_detect, csp, overlay, NULL))
    GST_ERROR_OBJECT (filter, "couldn't link elements");

  pad = gst_element_get_static_pad (face_detect, "sink");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (filter->sinkpad), pad))
    GST_ERROR_OBJECT (filter->sinkpad, "couldn't set sinkpad target");
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (overlay, "src");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (filter->srcpad), pad))
    GST_ERROR_OBJECT (filter->srcpad, "couldn't set srcpad target");
  gst_object_unref (pad);

  return TRUE;

/* ERRORS */
missing_element:
  {
    /* clean up */
    if (csp == NULL)
      GST_ERROR_OBJECT (filter, "ffmpegcolorspace element not found");
    else
      gst_object_unref (csp);

    if (face_detect == NULL)
      GST_ERROR_OBJECT (filter, "facedetect element not found (opencv plugin)");
    else
      gst_object_unref (face_detect);

    if (overlay == NULL)
      GST_ERROR_OBJECT (filter, "rsvgoverlay element not found (rsvg plugin)");
    else
      gst_object_unref (overlay);

    return FALSE;
  }
}

static GstStateChangeReturn
gst_face_overlay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFaceOverlay *filter = GST_FACEOVERLAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (filter->svg_overlay == NULL) {
        GST_ELEMENT_ERROR (filter, CORE, MISSING_PLUGIN, (NULL),
            ("Some required plugins are missing, probably either the opencv "
                "facedetect element or rsvgoverlay"));
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
gst_face_overlay_message_handler (GstBin * bin, GstMessage * message)
{
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT &&
      strcmp (gst_structure_get_name (message->structure), "facedetect") == 0) {
    GstFaceOverlay *filter = GST_FACEOVERLAY (bin);

    /* optionally draw the image once every two messages for better performance
     * filter->process_message = !filter->process_message;
     *  if(!filter->process_message)
     *    return;
     */

    guint x, y, width, height;
    int delta_x, delta_y, svg_x, svg_y, svg_width, svg_height;
    const GstStructure *face;
    int face_count;

    face_count =
        gst_value_list_get_size (gst_structure_get_value (message->structure,
            "faces"));

    /* The last face in the list seems to be the right one, objects mistakenly
     * detected as faces for a couple of frames seem to be in the list
     * beginning. TODO: needs confirmation. */
    face =
        gst_value_get_structure (gst_value_list_get_value
        (gst_structure_get_value (message->structure, "faces"),
            face_count - 1));
    gst_structure_get_uint (face, "x", &x);
    gst_structure_get_uint (face, "y", &y);
    gst_structure_get_uint (face, "width", &width);
    gst_structure_get_uint (face, "height", &height);

    /* Apply x and y offsets relative to face position and size.
     * Set image width and height as a fraction of face width and height.
     * Cast to int since face position and size will never be bigger than
     * G_MAX_INT and we may have negative values as svg_x or svg_y */

    delta_x = (int) (filter->x * (int) width);
    svg_x = (int) x + delta_x;

    delta_y = (int) (filter->y * (int) height);
    svg_y = (int) y + delta_y;

    svg_width = (int) width *filter->w;
    svg_height = (int) height *filter->h;

    g_object_set (filter->svg_overlay,
        "location", filter->location,
        "x", svg_x, "y", svg_y, "width", svg_width, "height", svg_height, NULL);

  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static void
gst_face_overlay_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "faceoverlay",
      "Filter/Editor/Video",
      "Overlays SVG graphics over a detected face in a video stream",
      "Laura Lucas Alday <lauralucas@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gst_face_overlay_class_init (GstFaceOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstBinClass *gstbin_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_face_overlay_set_property;
  gobject_class->get_property = gst_face_overlay_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location of SVG file to use for face overlay",
          "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_X,
      g_param_spec_float ("x", "face x offset",
          "Specify image x relative to detected face x.", -G_MAXFLOAT,
          G_MAXFLOAT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_Y,
      g_param_spec_float ("y", "face y offset",
          "Specify image y relative to detected face y.", -G_MAXFLOAT,
          G_MAXFLOAT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_W,
      g_param_spec_float ("w", "face width percent",
          "Specify image width relative to face width.", 0, G_MAXFLOAT, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_H,
      g_param_spec_float ("h", "face height percent",
          "Specify image height relative to face height.", 0, G_MAXFLOAT, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbin_class->handle_message =
      GST_DEBUG_FUNCPTR (gst_face_overlay_message_handler);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_face_overlay_change_state);
}

static void
gst_face_overlay_init (GstFaceOverlay * filter, GstFaceOverlayClass * gclass)
{
  GstPadTemplate *tmpl;

  filter->x = 0;
  filter->y = 0;
  filter->w = 1;
  filter->h = 1;
  filter->svg_overlay = NULL;
  filter->location = NULL;
  filter->process_message = TRUE;

  tmpl = gst_static_pad_template_get (&sink_factory);
  filter->sinkpad = gst_ghost_pad_new_no_target_from_template ("sink", tmpl);
  gst_object_unref (tmpl);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  tmpl = gst_static_pad_template_get (&src_factory);
  filter->srcpad = gst_ghost_pad_new_no_target_from_template ("src", tmpl);
  gst_object_unref (tmpl);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  gst_face_overlay_create_children (filter);
}

static void
gst_face_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFaceOverlay *filter = GST_FACEOVERLAY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      filter->location = g_value_dup_string (value);
      break;
    case PROP_X:
      filter->x = g_value_get_float (value);
      break;
    case PROP_Y:
      filter->y = g_value_get_float (value);
      break;
    case PROP_W:
      filter->w = g_value_get_float (value);
      break;
    case PROP_H:
      filter->h = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_face_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFaceOverlay *filter = GST_FACEOVERLAY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, filter->location);
      break;
    case PROP_X:
      g_value_set_float (value, filter->x);
      break;
    case PROP_Y:
      g_value_set_float (value, filter->y);
      break;
    case PROP_W:
      g_value_set_float (value, filter->w);
      break;
    case PROP_H:
      g_value_set_float (value, filter->h);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
faceoverlay_init (GstPlugin * faceoverlay)
{
  GST_DEBUG_CATEGORY_INIT (gst_face_overlay_debug, "faceoverlay",
      0, "SVG Face Overlay");

  return gst_element_register (faceoverlay, "faceoverlay", GST_RANK_NONE,
      GST_TYPE_FACEOVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "faceoverlay",
    "SVG Face Overlay",
    faceoverlay_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
