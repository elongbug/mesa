/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2017 Samsung Electronics co., Ltd. All Rights Reserved
 *
 * Based on platform_android, which has
 *
 * Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
 * Copyright (C) 2010-2011 LunarG Inc.
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gwan-gyeong Mun <elongbug@gmail.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xf86drm.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "egl_dri2.h"
#include "egl_dri2_fallbacks.h"
#include "loader.h"

static EGLBoolean
tizen_window_dequeue_buffer(struct dri2_egl_surface *dri2_surf)
{
   int width, height;

   dri2_surf->tbm_surface = tpl_surface_dequeue_buffer(dri2_surf->tpl_surface);

   if (!dri2_surf->tbm_surface)
      return EGL_FALSE;

   tbm_surface_internal_ref(dri2_surf->tbm_surface);

   tpl_surface_get_size(dri2_surf->tpl_surface, &width, &height);
   if (dri2_surf->base.Width != width || dri2_surf->base.Height != height) {
      dri2_surf->base.Width = width;
      dri2_surf->base.Height = height;
   }

   return EGL_TRUE;
}

static EGLBoolean
tizen_window_enqueue_buffer_with_damage(_EGLDisplay *disp,
                                        struct dri2_egl_surface *dri2_surf,
                                        const EGLint *rects,
                                        EGLint n_rects)
{
   tpl_result_t ret;

   /* To avoid blocking other EGL calls, release the display mutex before
    * we enter tizen_window_enqueue_buffer() and re-acquire the mutex upon
    * return.
    */
   mtx_unlock(&disp->Mutex);

   if (n_rects < 1 || rects == NULL) {
      /* if there is no damage, call the normal API tpl_surface_enqueue_buffer */
      ret = tpl_surface_enqueue_buffer(dri2_surf->tpl_surface,
                                       dri2_surf->tbm_surface);
   } else {
      /* if there are rectangles of damage region,
         call the API tpl_surface_enqueue_buffer_with_damage() */
      ret = tpl_surface_enqueue_buffer_with_damage(dri2_surf->tpl_surface,
                                                   dri2_surf->tbm_surface,
                                                   n_rects, rects);
   }

   if (ret != TPL_ERROR_NONE) {
      _eglLog(_EGL_WARNING, "%s : %d :tpl_surface_enqueue fail", __func__, __LINE__);
      goto cleanup;
   }

   tbm_surface_internal_unref(dri2_surf->tbm_surface);
   dri2_surf->tbm_surface = NULL;

   mtx_lock(&disp->Mutex);

   return EGL_TRUE;

cleanup:
   tbm_surface_internal_unref(dri2_surf->tbm_surface);
   dri2_surf->tbm_surface = NULL;
   mtx_lock(&disp->Mutex);

   return EGL_FALSE;
}

static EGLBoolean
tizen_window_enqueue_buffer(_EGLDisplay *disp, struct dri2_egl_surface *dri2_surf)
{
   return tizen_window_enqueue_buffer_with_damage(disp, dri2_surf, NULL, 0);
}

static void
tizen_window_cancel_buffer(_EGLDisplay *disp, struct dri2_egl_surface *dri2_surf)
{
   tizen_window_enqueue_buffer(disp, dri2_surf);
}

static _EGLSurface *
tizen_create_surface(_EGLDriver *drv, _EGLDisplay *disp, EGLint type,
                     _EGLConfig *conf, void *native_window,
                     const EGLint *attrib_list)
{
   __DRIcreateNewDrawableFunc createNewDrawable;
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct dri2_egl_config *dri2_conf = dri2_egl_config(conf);
   struct dri2_egl_surface *dri2_surf;
   const __DRIconfig *config;
   tpl_surface_type_t tpl_surf_type = TPL_SURFACE_ERROR;
   tpl_result_t ret = TPL_ERROR_INVALID_PARAMETER;

   dri2_surf = calloc(1, sizeof *dri2_surf);
   if (!dri2_surf) {
      _eglError(EGL_BAD_ALLOC, "tizen_create_surface");
      return NULL;
   }

   if (!_eglInitSurface(&dri2_surf->base, disp, type, conf, attrib_list))
      goto cleanup_surface;

   config = dri2_get_dri_config(dri2_conf, type,
                                dri2_surf->base.GLColorspace);
   if (!config)
      goto cleanup_surface;

   if (type == EGL_WINDOW_BIT) {
      unsigned int alpha, depth;

      if (!native_window) {
         _eglError(EGL_BAD_NATIVE_WINDOW, "tizen_create_surface needs vaild native window");
         goto cleanup_surface;
      }
      dri2_surf->native_win = native_window;

      dri2_dpy->core->getConfigAttrib(config, __DRI_ATTRIB_DEPTH_SIZE, &depth);
      dri2_dpy->core->getConfigAttrib(config, __DRI_ATTRIB_ALPHA_SIZE, &alpha);

      ret = tpl_display_get_native_window_info(dri2_dpy->tpl_display,
                                               (tpl_handle_t)native_window,
                                               &dri2_surf->base.Width,
                                               &dri2_surf->base.Height,
                                               &dri2_surf->tbm_format,
                                               depth, alpha);

      if (ret != TPL_ERROR_NONE || dri2_surf->tbm_format == 0) {
         _eglError(EGL_BAD_NATIVE_WINDOW, "tizen_create_surface fails on tpl_display_get_native_window_info()");
         goto cleanup_surface;
      }

      tpl_surf_type = TPL_SURFACE_TYPE_WINDOW;
   } else if (type == EGL_PIXMAP_BIT) {

      if (!native_window) {
         _eglError(EGL_BAD_NATIVE_PIXMAP, "tizen_create_surface needs valid native pixmap");
         goto cleanup_surface;
      }
      ret = tpl_display_get_native_pixmap_info(dri2_dpy->tpl_display,
                                               (tpl_handle_t)native_window,
                                               &dri2_surf->base.Width,
                                               &dri2_surf->base.Height,
                                               &dri2_surf->tbm_format);

      if (ret != TPL_ERROR_NONE || dri2_surf->tbm_format == 0) {
         _eglError(EGL_BAD_NATIVE_PIXMAP, "tizen_create_surface fails on tpl_display_get_native_pixmap_info");
         goto cleanup_surface;
      }

      tpl_surf_type = TPL_SURFACE_TYPE_PIXMAP;
   } else {
      _eglError(EGL_BAD_NATIVE_WINDOW, "tizen_create_surface does not support PBuffer");
      goto cleanup_surface;
   }

   dri2_surf->tpl_surface = tpl_surface_create(dri2_dpy->tpl_display,
                                               (tpl_handle_t)native_window,
                                               tpl_surf_type,
                                               dri2_surf->tbm_format);
   if (!dri2_surf->tpl_surface)
      goto cleanup_surface;

   createNewDrawable = dri2_dpy->swrast->createNewDrawable;

   dri2_surf->dri_drawable = (*createNewDrawable)(dri2_dpy->dri_screen, config,
                                                  dri2_surf);
    if (dri2_surf->dri_drawable == NULL) {
       _eglError(EGL_BAD_ALLOC, "createNewDrawable");
       goto cleanup_tpl_surface;
    }

   return &dri2_surf->base;

cleanup_tpl_surface:
   tpl_object_unreference((tpl_object_t *)dri2_surf->tpl_surface);
cleanup_surface:
   free(dri2_surf);

   return NULL;
}

static _EGLSurface *
tizen_create_window_surface(_EGLDriver *drv, _EGLDisplay *disp,
                            _EGLConfig *conf, void *native_window,
                            const EGLint *attrib_list)
{
   return tizen_create_surface(drv, disp, EGL_WINDOW_BIT, conf,
                               native_window, attrib_list);
}

static _EGLSurface *
tizen_create_pixmap_surface(_EGLDriver *drv, _EGLDisplay *disp,
                            _EGLConfig *conf, void *native_pixmap,
                            const EGLint *attrib_list)
{
   return tizen_create_surface(drv, disp, EGL_PIXMAP_BIT, conf,
                               native_pixmap, attrib_list);
}

static EGLBoolean
tizen_destroy_surface(_EGLDriver *drv, _EGLDisplay *disp, _EGLSurface *surf)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(surf);

   if (dri2_surf->base.Type == EGL_WINDOW_BIT && dri2_surf->tbm_surface)
      tizen_window_cancel_buffer(disp, dri2_surf);

   dri2_dpy->core->destroyDrawable(dri2_surf->dri_drawable);

   tpl_object_unreference((tpl_object_t *)dri2_surf->tpl_surface);

   free(dri2_surf);

   return EGL_TRUE;
}

static int
update_buffers(struct dri2_egl_surface *dri2_surf)
{
   if (dri2_surf->base.Type != EGL_WINDOW_BIT)
      return 0;

   /* try to dequeue the next back buffer */
   if (!dri2_surf->tbm_surface && !tizen_window_dequeue_buffer(dri2_surf)) {
      _eglLog(_EGL_WARNING, "Could not dequeue buffer from native window");
      return -1;
   }

   return 0;
}

static EGLBoolean
tizen_swap_buffers_with_damage(_EGLDriver *drv, _EGLDisplay *disp,
                               _EGLSurface *draw, const EGLint *rects,
                               EGLint n_rects)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(draw);

   if (dri2_surf->base.Type != EGL_WINDOW_BIT)
      return EGL_TRUE;

   if (dri2_surf->tbm_surface)
      tizen_window_enqueue_buffer_with_damage(disp, dri2_surf, rects, n_rects);

   dri2_dpy->core->swapBuffers(dri2_surf->dri_drawable);

   return EGL_TRUE;
}

static EGLBoolean
tizen_swap_buffers(_EGLDriver *drv, _EGLDisplay *disp, _EGLSurface *draw)
{
   return tizen_swap_buffers_with_damage (drv, disp, draw, NULL, 0);
}

static EGLBoolean
tizen_query_surface(_EGLDriver *drv, _EGLDisplay *dpy, _EGLSurface *surf,
                    EGLint attribute, EGLint *value)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(dpy);
   struct dri2_egl_surface *dri2_surf = dri2_egl_surface(surf);
   int width = 0, height = 0;

   if (dri2_surf->base.Type == EGL_WINDOW_BIT && dri2_surf->native_win) {
      if (tpl_display_get_native_window_info(dri2_dpy->tpl_display,
                                             dri2_surf->native_win,
                                             &width, &height,
                                             NULL, 0, 0) != TPL_ERROR_NONE)
         return EGL_FALSE;

      switch (attribute) {
      case EGL_WIDTH:
         *value = width;
         return EGL_TRUE;
      case EGL_HEIGHT:
         *value = height;
         return EGL_TRUE;
      default:
         break;
      }
   }

   return _eglQuerySurface(drv, dpy, surf, attribute, value);
}

static int
tizen_swrast_get_stride_for_format(tbm_format format, int w)
{
   switch (format) {
   case TBM_FORMAT_RGB565:
      return 2 * w;
   case TBM_FORMAT_BGRA8888:
   case TBM_FORMAT_RGBA8888:
   case TBM_FORMAT_RGBX8888:
   default:
      return 4 * w;
   }
}

static void
tizen_swrast_get_drawable_info(__DRIdrawable * draw,
                               int *x, int *y, int *w, int *h,
                               void *loaderPrivate)
{
   struct dri2_egl_surface *dri2_surf = loaderPrivate;

   if (update_buffers(dri2_surf) < 0)
      return;

   *x = 0;
   *y = 0;
   *w = dri2_surf->base.Width;
   *h = dri2_surf->base.Height;
}

static void
tizen_swrast_get_image(__DRIdrawable * read,
                       int x, int y, int w, int h,
                       char *data, void *loaderPrivate)
{
   struct dri2_egl_surface *dri2_surf = loaderPrivate;
   tbm_surface_info_s surf_info;
   int ret = TBM_SURFACE_ERROR_NONE;
   int internal_stride, stride, i;
   int x_bytes, w_bytes;
   char *src, *dst;

   if (update_buffers(dri2_surf) < 0)
      return;

   ret = tbm_surface_map(dri2_surf->tbm_surface, TBM_SURF_OPTION_READ, &surf_info);

   if (ret != TBM_SURFACE_ERROR_NONE) {
      _eglLog(_EGL_WARNING, "Could not tbm_surface_map");
      return;
   }

   x_bytes = tizen_swrast_get_stride_for_format(dri2_surf->tbm_format, x);
   w_bytes = tizen_swrast_get_stride_for_format(dri2_surf->tbm_format, w);
   internal_stride = surf_info.planes[0].stride;
   stride = w_bytes;

   dst = data;
   src = (char*)surf_info.planes[0].ptr + x_bytes + (y * internal_stride);

   for (i = 0; i < h; i++) {
      memcpy(dst, src, w_bytes);
      dst += stride;
      src += internal_stride;
   }

   tbm_surface_unmap(dri2_surf->tbm_surface);
}

static void
tizen_swrast_put_image2(__DRIdrawable * draw, int op,
                        int x, int y, int w, int h, int stride,
                        char *data, void *loaderPrivate)
{
   struct dri2_egl_surface *dri2_surf = loaderPrivate;
   tbm_surface_info_s surf_info;
   int ret = TBM_SURFACE_ERROR_NONE;
   int internal_stride, i;
   int x_bytes, w_bytes;
   char *src, *dst;

   if (op != __DRI_SWRAST_IMAGE_OP_DRAW && op != __DRI_SWRAST_IMAGE_OP_SWAP)
      return;

   if (dri2_surf->base.Type == EGL_WINDOW_BIT) {
      if (update_buffers(dri2_surf) < 0) {
         _eglLog(_EGL_WARNING, "Could not get native buffer");
         return;
      }

      ret = tbm_surface_map(dri2_surf->tbm_surface, TBM_SURF_OPTION_WRITE, &surf_info);
      if (ret != TBM_SURFACE_ERROR_NONE) {
         _eglLog(_EGL_WARNING, "Could not tbm_surface_map");
         return;
      }

      x_bytes = tizen_swrast_get_stride_for_format(dri2_surf->tbm_format, x);
      w_bytes = tizen_swrast_get_stride_for_format(dri2_surf->tbm_format, w);
      internal_stride = surf_info.planes[0].stride;

      dst = (char*)surf_info.planes[0].ptr + x_bytes + (y * internal_stride);
      src = data;

      for (i = 0; i < h; i++) {
         memcpy(dst, src, w_bytes);
         dst += internal_stride;
         src += stride;
      }

      tbm_surface_unmap(dri2_surf->tbm_surface);
   }
}

static void
tizen_swrast_put_image(__DRIdrawable * draw, int op,
                         int x, int y, int w, int h,
                         char *data, void *loaderPrivate)
{
   struct dri2_egl_surface *dri2_surf = loaderPrivate;
   int stride;

   if (dri2_surf->base.Type == EGL_WINDOW_BIT) {
      if (update_buffers(dri2_surf) < 0) {
         _eglLog(_EGL_WARNING, "Could not get native buffer");
         return;
      }

      stride = tizen_swrast_get_stride_for_format(dri2_surf->tbm_format, w);
      tizen_swrast_put_image2(draw, op, x, y, w, h, stride, data, loaderPrivate);
   }
}

static int
tizen_add_configs_for_surface_type(_EGLDisplay *dpy, int surface_type,
                                   int *idx, int cfg_cnt)
{
   struct dri2_egl_display *dri2_dpy = dri2_egl_display(dpy);
   tpl_surface_type_t tpl_surface_type;
   static const struct {
      unsigned int rgba_masks[4];
      struct {
         unsigned alpha;
         unsigned red;
         unsigned green;
         unsigned blue;
      } size;
   } visuals[] = {
      { { 0xff0000, 0xff00, 0x00ff, 0xff000000 }, { 0, 8, 8, 8 } },
      { { 0xff0000, 0xff00, 0x00ff, 0 }, { 8, 8, 8, 8 } },
   };
   int i;

   if (surface_type == EGL_WINDOW_BIT)
      tpl_surface_type = TPL_SURFACE_TYPE_WINDOW;
   else if (surface_type == EGL_PIXMAP_BIT)
      tpl_surface_type = TPL_SURFACE_TYPE_PIXMAP;
   else {
      _eglLog(_EGL_WARNING, "Not surpported Surface Type: %d", surface_type);
      return cfg_cnt;
   }

   for (i = *idx; dri2_dpy->driver_configs[i]; i++) {
      for (int j = 0; j < ARRAY_SIZE(visuals); j++) {
         struct dri2_egl_config *dri2_conf;
         unsigned int depth = 32;
         tpl_bool_t is_slow;
         EGLint config_attrs[] = {
            EGL_NATIVE_VISUAL_ID, 0,
            EGL_NONE,
         };
         tpl_result_t res;

         res = tpl_display_query_config(dri2_dpy->tpl_display, tpl_surface_type,
                                        visuals[j].size.red, visuals[j].size.green,
                                        visuals[j].size.blue, visuals[j].size.alpha,
                                        depth, &config_attrs[1], &is_slow);

         if (res != TPL_ERROR_NONE)
            continue;

         dri2_conf = dri2_add_config(dpy, dri2_dpy->driver_configs[i],
                                     cfg_cnt + 1, surface_type, config_attrs,
                                     visuals[j].rgba_masks);

         if (dri2_conf && (dri2_conf->base.ConfigID == cfg_cnt + 1))
            cfg_cnt++;
      }
   }

   *idx = i;
   return cfg_cnt;
}

static EGLBoolean
tizen_add_configs(_EGLDriver *drv, _EGLDisplay *dpy)
{
   int idx = 0;
   int cfg_cnt = 0;

   cfg_cnt = tizen_add_configs_for_surface_type(dpy, EGL_WINDOW_BIT, &idx, cfg_cnt);
   cfg_cnt = tizen_add_configs_for_surface_type(dpy, EGL_PIXMAP_BIT, &idx, cfg_cnt);

   return (cfg_cnt != 0);
}

static const struct dri2_egl_display_vtbl tizen_display_vtbl = {
   .authenticate = NULL,
   .create_window_surface = tizen_create_window_surface,
   .create_pixmap_surface = tizen_create_pixmap_surface,
   .create_pbuffer_surface = dri2_fallback_create_pbuffer_surface,
   .destroy_surface = tizen_destroy_surface,
   .create_image = dri2_create_image_khr,
   .swap_buffers = tizen_swap_buffers,
   .swap_buffers_with_damage = tizen_swap_buffers_with_damage,
   .swap_buffers_region = dri2_fallback_swap_buffers_region,
   .post_sub_buffer = dri2_fallback_post_sub_buffer,
   .copy_buffers = dri2_fallback_copy_buffers,
   .query_buffer_age = dri2_fallback_query_buffer_age,
   .query_surface = tizen_query_surface,
   .create_wayland_buffer_from_image = dri2_fallback_create_wayland_buffer_from_image,
   .get_sync_values = dri2_fallback_get_sync_values,
   .get_dri_drawable = dri2_surface_get_dri_drawable,
};

static const __DRIswrastLoaderExtension tizen_swrast_loader_extension = {
   .base = { __DRI_SWRAST_LOADER, 2 },

   .getDrawableInfo = tizen_swrast_get_drawable_info,
   .putImage        = tizen_swrast_put_image,
   .getImage        = tizen_swrast_get_image,
   .putImage2       = tizen_swrast_put_image2,
};

static const __DRIextension *tizen_swrast_loader_extensions[] = {
   &tizen_swrast_loader_extension.base,
   NULL,
};

EGLBoolean
dri2_initialize_tizen(_EGLDriver *drv, _EGLDisplay *dpy)
{
   struct dri2_egl_display *dri2_dpy;
   tpl_display_t *tpl_display = NULL;
   const char *err;
   int tbm_bufmgr_fd = -1;

   loader_set_logger(_eglLog);

   dri2_dpy = calloc(1, sizeof *dri2_dpy);
   if (!dri2_dpy)
      return _eglError(EGL_BAD_ALLOC, "eglInitialize");

   dri2_dpy->fd = -1;
   dpy->DriverData = (void *) dri2_dpy;

   /* The TPL_BACKEND_UNKNOWN type for tpl_display decides the tpl's "Display Backend"
    * as a PlatformDiplay at runtime.
    */
   tpl_display = tpl_display_create(TPL_BACKEND_UNKNOWN, dpy->PlatformDisplay);
   if (!tpl_display) {
      err = "DRI2: failed to create tpl_display";
      goto cleanup;
   }
   dri2_dpy->tpl_display = tpl_display;

   /* Get tbm_bufmgr's fd */
   tbm_bufmgr_fd = tbm_drm_helper_get_fd();

   if (tbm_bufmgr_fd == -1) {
      err = "DRI2: failed to get tbm_bufmgr fd";
      goto cleanup;
   }

   dri2_dpy->fd = tbm_bufmgr_fd;
   dri2_dpy->driver_name = strdup("swrast");
   if (!dri2_load_driver_swrast(dpy)) {
      err = "DRI2: failed to load swrast driver";
      goto cleanup;
   }
   dri2_dpy->loader_extensions = tizen_swrast_loader_extensions;

   if (!dri2_create_screen(dpy)) {
      err = "DRI2: failed to create screen";
      goto cleanup;
   }

   if (!dri2_setup_extensions(dpy)) {
      err = "DRI2: failed to setup extensions";
      goto cleanup;
   }

   dri2_setup_screen(dpy);

   if (!tizen_add_configs(drv, dpy)) {
      err = "DRI2: failed to add configs";
      goto cleanup;
   }

   /* Fill vtbl last to prevent accidentally calling virtual function during
    * initialization.
    */
   dri2_dpy->vtbl = &tizen_display_vtbl;

   return EGL_TRUE;

cleanup:
   dri2_display_destroy(dpy);

   return _eglError(EGL_NOT_INITIALIZED, err);
}
