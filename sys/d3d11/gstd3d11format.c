/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d11format.h"
#include "gstd3d11utils.h"
#include "gstd3d11device.h"
#include "gstd3d11memory.h"

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_format_debug);
#define GST_CAT_DEFAULT gst_d3d11_format_debug

guint
gst_d3d11_dxgi_format_n_planes (DXGI_FORMAT format)
{
  switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
      return 1;
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      return 2;
    default:
      break;
  }

  return 0;
}

gboolean
gst_d3d11_dxgi_format_get_size (DXGI_FORMAT format, guint width, guint height,
    guint pitch, gsize offset[GST_VIDEO_MAX_PLANES],
    gint stride[GST_VIDEO_MAX_PLANES], gsize * size)
{
  g_return_val_if_fail (format != DXGI_FORMAT_UNKNOWN, FALSE);

  switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
      offset[0] = 0;
      stride[0] = pitch;
      *size = pitch * height;
      break;
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      offset[0] = 0;
      stride[0] = pitch;
      offset[1] = offset[0] + stride[0] * height;
      stride[1] = pitch;
      *size = offset[1] + stride[1] * GST_ROUND_UP_2 (height / 2);
      break;
    default:
      return FALSE;
  }

  GST_LOG ("Calculated buffer size: %" G_GSIZE_FORMAT
      " (dxgi format:%d, %dx%d, Pitch %d)",
      *size, format, width, height, pitch);

  return TRUE;
}

/**
 * gst_d3d11_device_get_supported_caps:
 * @device: a #GstD3DDevice
 * @flags: D3D11_FORMAT_SUPPORT flags
 *
 * Check supported format with given flags
 *
 * Returns: a #GstCaps representing supported format
 */
GstCaps *
gst_d3d11_device_get_supported_caps (GstD3D11Device * device,
    D3D11_FORMAT_SUPPORT flags)
{
  ID3D11Device *d3d11_device;
  HRESULT hr;
  gint i;
  GValue v_list = G_VALUE_INIT;
  GstCaps *supported_caps;
  static const GstVideoFormat format_list[] = {
    GST_VIDEO_FORMAT_BGRA,
    GST_VIDEO_FORMAT_RGBA,
    GST_VIDEO_FORMAT_RGB10A2_LE,
    GST_VIDEO_FORMAT_VUYA,
    GST_VIDEO_FORMAT_NV12,
    GST_VIDEO_FORMAT_P010_10LE,
    GST_VIDEO_FORMAT_P016_LE,
    GST_VIDEO_FORMAT_I420,
    GST_VIDEO_FORMAT_I420_10LE,
  };

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  d3d11_device = gst_d3d11_device_get_device_handle (device);
  g_value_init (&v_list, GST_TYPE_LIST);

  for (i = 0; i < G_N_ELEMENTS (format_list); i++) {
    UINT format_support = 0;
    GstVideoFormat format;
    const GstD3D11Format *d3d11_format;

    d3d11_format = gst_d3d11_device_format_from_gst (device, format_list[i]);
    if (!d3d11_format || d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN)
      continue;

    format = d3d11_format->format;
    hr = ID3D11Device_CheckFormatSupport (d3d11_device,
        d3d11_format->dxgi_format, &format_support);

    if (SUCCEEDED (hr) && ((format_support & flags) == flags)) {
      GValue v_str = G_VALUE_INIT;
      g_value_init (&v_str, G_TYPE_STRING);

      GST_LOG_OBJECT (device, "d3d11 device can support %s with flags 0x%x",
          gst_video_format_to_string (format), flags);
      g_value_set_string (&v_str, gst_video_format_to_string (format));
      gst_value_list_append_and_take_value (&v_list, &v_str);
    }
  }

  supported_caps = gst_caps_new_simple ("video/x-raw",
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  gst_caps_set_value (supported_caps, "format", &v_list);
  g_value_unset (&v_list);

  gst_caps_set_features_simple (supported_caps,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY));

  return supported_caps;
}
