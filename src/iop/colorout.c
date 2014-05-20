/*
		This file is part of darktable,
		copyright (c) 2009--2011 johannes hanika.
		copyright (c) 2011 henrik andersson.

		darktable is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 3 of the License, or
		(at your option) any later version.

		darktable is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "develop/develop.h"
#include "iop/colorout.h"
#include "bauhaus/bauhaus.h"
#include "control/control.h"
#include "control/conf.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "common/colorspaces.h"
#include "common/opencl.h"

#include <xmmintrin.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>

DT_MODULE_INTROSPECTION(3, dt_iop_colorout_params_t)

static gchar *_get_profile_from_pos(GList *profiles, int pos);
static gchar *_get_display_profile_from_pos(GList *profiles, int pos);


typedef enum dt_iop_colorout_softproof_t
{
  DT_SOFTPROOF_DISABLED            = 0,
  DT_SOFTPROOF_ENABLED             = 1,
  DT_SOFTPROOF_GAMUTCHECK          = 2
}
dt_iop_colorout_softproof_t;

typedef struct dt_iop_colorout_global_data_t
{
  int kernel_colorout;
}
dt_iop_colorout_global_data_t;

typedef struct dt_iop_colorout_params_t
{
  char iccprofile[DT_IOP_COLOR_ICC_LEN];
  char displayprofile[DT_IOP_COLOR_ICC_LEN];

  dt_iop_color_intent_t intent;
  dt_iop_color_intent_t displayintent;

  char softproof_enabled;
  char softproofprofile[DT_IOP_COLOR_ICC_LEN];
  dt_iop_color_intent_t softproofintent; /// NOTE: Not used for now but reserved for future use
}
dt_iop_colorout_params_t;

typedef struct dt_iop_colorout_gui_data_t
{
  gint softproof_enabled;
  GtkWidget *cbox1, *cbox2, *cbox3, *cbox4,*cbox5;
  GList *profiles;

}
dt_iop_colorout_gui_data_t;


const char
*name()
{
  return _("output color profile");
}

int
groups ()
{
  return IOP_GROUP_COLOR;
}

int
flags ()
{
  return IOP_FLAGS_ALLOW_TILING | IOP_FLAGS_ONE_INSTANCE;
}


static gboolean key_softproof_callback(GtkAccelGroup *accel_group,
                                       GObject *acceleratable,
                                       guint keyval, GdkModifierType modifier,
                                       gpointer data)
{
  dt_iop_module_t* self = (dt_iop_module_t*)data;
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)self->params;

  if(p->softproof_enabled == DT_SOFTPROOF_ENABLED)
    p->softproof_enabled = DT_SOFTPROOF_DISABLED;
  else
    p->softproof_enabled = DT_SOFTPROOF_ENABLED;

  g->softproof_enabled = p->softproof_enabled;

  if(p->softproof_enabled)
  {
    int pos = dt_bauhaus_combobox_get(g->cbox5);
    gchar *filename = _get_profile_from_pos(g->profiles, pos);
    if (filename)
      g_strlcpy(p->softproofprofile, filename, sizeof(p->softproofprofile));
  }

  dt_dev_add_history_item(darktable.develop, self, TRUE);
  dt_control_queue_redraw_center();
  return TRUE;
}


static gboolean key_gamutcheck_callback(GtkAccelGroup *accel_group,
                                        GObject *acceleratable,
                                        guint keyval, GdkModifierType modifier,
                                        gpointer data)
{
  dt_iop_module_t* self = (dt_iop_module_t*)data;
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)self->params;


  if(p->softproof_enabled == DT_SOFTPROOF_GAMUTCHECK)
    p->softproof_enabled = DT_SOFTPROOF_DISABLED;
  else
    p->softproof_enabled = DT_SOFTPROOF_GAMUTCHECK;

  g->softproof_enabled = p->softproof_enabled;

  if(p->softproof_enabled)
  {
    int pos = dt_bauhaus_combobox_get(g->cbox5);
    gchar *filename = _get_profile_from_pos(g->profiles, pos);
    if (filename)
      g_strlcpy(p->softproofprofile, filename, sizeof(p->softproofprofile));
  }

  dt_dev_add_history_item(darktable.develop, self, TRUE);
  dt_control_queue_redraw_center();
  return TRUE;
}



int
legacy_params (dt_iop_module_t *self, const void *const old_params, const int old_version, void *new_params, const int new_version)
{
  /*  if(old_version == 1 && new_version == 2)
  {
    dt_iop_colorout_params_t *o = (dt_iop_colorout_params_t *)old_params;
    dt_iop_colorout_params_t *n = (dt_iop_colorout_params_t *)new_params;
    memcpy(n,o,sizeof(dt_iop_colorout_params_t));
    n->seq = 0;
    return 0;
    }*/
  if(old_version == 2 && new_version == 3)
  {
    dt_iop_colorout_params_t *o = (dt_iop_colorout_params_t *)old_params;
    dt_iop_colorout_params_t *n = (dt_iop_colorout_params_t *)new_params;
    memcpy(n,o,sizeof(dt_iop_colorout_params_t));
    n->softproof_enabled = DT_SOFTPROOF_DISABLED;
    n->softproofintent = 0;
    g_strlcpy(n->softproofprofile,"sRGB",sizeof(n->softproofprofile));
    return 0;
  }

  return 1;
}

void
init_global(dt_iop_module_so_t *module)
{
  const int program = 2; // basic.cl, from programs.conf
  dt_iop_colorout_global_data_t *gd = (dt_iop_colorout_global_data_t *)malloc(sizeof(dt_iop_colorout_global_data_t));
  module->data = gd;
  gd->kernel_colorout = dt_opencl_create_kernel(program, "colorout");
}

void
cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_colorout_global_data_t *gd = (dt_iop_colorout_global_data_t *)module->data;
  dt_opencl_free_kernel(gd->kernel_colorout);
  free(module->data);
  module->data = NULL;
}

static void
intent_changed (GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)self->params;
  p->intent = (dt_iop_color_intent_t)dt_bauhaus_combobox_get(widget);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void
display_intent_changed (GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)self->params;
  p->displayintent = (dt_iop_color_intent_t)dt_bauhaus_combobox_get(widget);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static gchar *_get_profile_from_pos(GList *profiles, int pos)
{
  while(profiles)
  {
    dt_iop_color_profile_t *pp = (dt_iop_color_profile_t *)profiles->data;
    if(pp->pos == pos)
      return pp->filename;
    profiles = g_list_next(profiles);
  }
  return NULL;
}

static gchar *_get_display_profile_from_pos(GList *profiles, int pos)
{
  while(profiles)
  {
    dt_iop_color_profile_t *pp = (dt_iop_color_profile_t *)profiles->data;
    if(pp->display_pos == pos)
      return pp->filename;
    profiles = g_list_next(profiles);
  }
  return NULL;
}


static void
output_profile_changed(GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)self->params;
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
  int pos = dt_bauhaus_combobox_get(widget);

  gchar *filename = _get_profile_from_pos(g->profiles, pos);
  if (filename)
  {
    g_strlcpy(p->iccprofile, filename, sizeof(p->iccprofile));
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    return;
  }

  fprintf(stderr, "[colorout] color profile %s seems to have disappeared!\n", p->iccprofile);
}

static void
softproof_profile_changed (GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)self->params;
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
  int pos = dt_bauhaus_combobox_get(widget);
  gchar *filename = _get_profile_from_pos(g->profiles, pos);
  if (filename)
  {
    g_strlcpy(p->softproofprofile, filename, sizeof(p->softproofprofile));
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    return;
  }

  fprintf(stderr, "[colorout] softprofile %s seems to have disappeared!\n", p->softproofprofile);
}

static void
display_profile_changed (GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)self->params;
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
  int pos = dt_bauhaus_combobox_get(widget);
  gchar *filename = _get_display_profile_from_pos(g->profiles, pos);
  if (filename)
  {
    g_strlcpy(p->displayprofile, filename, sizeof(p->displayprofile));
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    return;
  }

  // should really never happen.
  fprintf(stderr, "[colorout] display color profile %s seems to have disappeared!\n", p->displayprofile);
}

static void
_signal_profile_changed(gpointer instance, gpointer user_data)
{
  dt_develop_t *dev = (dt_develop_t*)user_data;
  if(!dev->gui_attached || dev->gui_leaving) return;
  dt_dev_reprocess_center(dev);
}

#if 1
static float
lerp_lut(const float *const lut, const float v)
{
  // TODO: check if optimization is worthwhile!
  const float ft = CLAMPS(v*(LUT_SAMPLES-1), 0, LUT_SAMPLES-1);
  const int t = ft < LUT_SAMPLES-2 ? ft : LUT_SAMPLES-2;
  const float f = ft - t;
  const float l1 = lut[t];
  const float l2 = lut[t+1];
  return l1*(1.0f-f) + l2*f;
}
#endif

#ifdef HAVE_OPENCL
int
process_cl (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_colorout_data_t *d = (dt_iop_colorout_data_t *)piece->data;
  dt_iop_colorout_global_data_t *gd = (dt_iop_colorout_global_data_t *)self->data;
  cl_mem dev_m = NULL, dev_r = NULL, dev_g = NULL, dev_b = NULL, dev_coeffs = NULL;

  cl_int err = -999;
  const int devid = piece->pipe->devid;
  const int width = roi_in->width;
  const int height = roi_in->height;

  size_t sizes[] = { ROUNDUPWD(width), ROUNDUPHT(height), 1};

  dev_m = dt_opencl_copy_host_to_device_constant(devid, sizeof(float)*9, d->cmatrix);
  if (dev_m == NULL) goto error;
  dev_r = dt_opencl_copy_host_to_device(devid, d->lut[0], 256, 256, sizeof(float));
  if (dev_r == NULL) goto error;
  dev_g = dt_opencl_copy_host_to_device(devid, d->lut[1], 256, 256, sizeof(float));
  if (dev_g == NULL) goto error;
  dev_b = dt_opencl_copy_host_to_device(devid, d->lut[2], 256, 256, sizeof(float));
  if (dev_b == NULL) goto error;
  dev_coeffs = dt_opencl_copy_host_to_device_constant(devid, sizeof(float)*3*3, (float *)d->unbounded_coeffs);
  if (dev_coeffs == NULL) goto error;
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 0, sizeof(cl_mem), (void *)&dev_in);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 1, sizeof(cl_mem), (void *)&dev_out);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 2, sizeof(int), (void *)&width);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 3, sizeof(int), (void *)&height);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 4, sizeof(cl_mem), (void *)&dev_m);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 5, sizeof(cl_mem), (void *)&dev_r);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 6, sizeof(cl_mem), (void *)&dev_g);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 7, sizeof(cl_mem), (void *)&dev_b);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorout, 8, sizeof(cl_mem), (void *)&dev_coeffs);
  err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_colorout, sizes);
  if(err != CL_SUCCESS) goto error;
  dt_opencl_release_mem_object(dev_m);
  dt_opencl_release_mem_object(dev_r);
  dt_opencl_release_mem_object(dev_g);
  dt_opencl_release_mem_object(dev_b);
  dt_opencl_release_mem_object(dev_coeffs);
  return TRUE;

error:
  if (dev_m != NULL) dt_opencl_release_mem_object(dev_m);
  if (dev_r != NULL) dt_opencl_release_mem_object(dev_r);
  if (dev_g != NULL) dt_opencl_release_mem_object(dev_g);
  if (dev_b != NULL) dt_opencl_release_mem_object(dev_b);
  if (dev_coeffs != NULL) dt_opencl_release_mem_object(dev_coeffs);
  dt_print(DT_DEBUG_OPENCL, "[opencl_colorout] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif

static inline __m128
lab_f_inv_m(const __m128 x)
{
  const __m128 epsilon = _mm_set1_ps(0.20689655172413796f); // cbrtf(216.0f/24389.0f);
  const __m128 kappa_rcp_x16   = _mm_set1_ps(16.0f*27.0f/24389.0f);
  const __m128 kappa_rcp_x116   = _mm_set1_ps(116.0f*27.0f/24389.0f);

  // x > epsilon
  const __m128 res_big   = _mm_mul_ps(_mm_mul_ps(x,x),x);
  // x <= epsilon
  const __m128 res_small = _mm_sub_ps(_mm_mul_ps(kappa_rcp_x116,x),kappa_rcp_x16);

  // blend results according to whether each component is > epsilon or not
  const __m128 mask = _mm_cmpgt_ps(x,epsilon);
  return _mm_or_ps(_mm_and_ps(mask,res_big),_mm_andnot_ps(mask,res_small));
}

static inline __m128
dt_Lab_to_XYZ_SSE(const __m128 Lab)
{
  const __m128 d50    = _mm_set_ps(0.0f, 0.8249f, 1.0f, 0.9642f);
  const __m128 coef   = _mm_set_ps(0.0f,-1.0f/200.0f,1.0f/116.0f,1.0f/500.0f);
  const __m128 offset = _mm_set1_ps(0.137931034f);

  // last component ins shuffle taken from 1st component of Lab to make sure it is not nan, so it will become 0.0f in f
  const __m128 f = _mm_mul_ps(_mm_shuffle_ps(Lab,Lab,_MM_SHUFFLE(0,2,0,1)),coef);

  return _mm_mul_ps(d50,lab_f_inv_m(_mm_add_ps(_mm_add_ps(f,_mm_shuffle_ps(f,f,_MM_SHUFFLE(1,1,3,1))),offset)));
}

void
process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *ivoid, void *ovoid, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  const dt_iop_colorout_data_t *const d = (dt_iop_colorout_data_t *)piece->data;
  const int ch = piece->colors;
  const int gamutcheck = (d->softproof_enabled == DT_SOFTPROOF_GAMUTCHECK);

  if(!isnan(d->cmatrix[0]))
  {
    //fprintf(stderr,"Using cmatrix codepath\n");
    // convert to rgb using matrix
#ifdef _OPENMP
    #pragma omp parallel for schedule(static) default(none) shared(roi_in,roi_out, ivoid, ovoid)
#endif
    for(int j=0; j<roi_out->height; j++)
    {

      float *in  = (float*)ivoid + (size_t)ch*roi_in->width *j;
      float *out = (float*)ovoid + (size_t)ch*roi_out->width*j;
      const __m128 m0 = _mm_set_ps(0.0f,d->cmatrix[6],d->cmatrix[3],d->cmatrix[0]);
      const __m128 m1 = _mm_set_ps(0.0f,d->cmatrix[7],d->cmatrix[4],d->cmatrix[1]);
      const __m128 m2 = _mm_set_ps(0.0f,d->cmatrix[8],d->cmatrix[5],d->cmatrix[2]);

      for(int i=0; i<roi_out->width; i++, in+=ch, out+=ch )
      {
        const __m128 xyz = dt_Lab_to_XYZ_SSE(_mm_load_ps(in));
        const __m128 t = _mm_add_ps(_mm_mul_ps(m0,_mm_shuffle_ps(xyz,xyz,_MM_SHUFFLE(0,0,0,0))),_mm_add_ps(_mm_mul_ps(m1,_mm_shuffle_ps(xyz,xyz,_MM_SHUFFLE(1,1,1,1))),_mm_mul_ps(m2,_mm_shuffle_ps(xyz,xyz,_MM_SHUFFLE(2,2,2,2)))));

        _mm_stream_ps(out,t);
      }
    }
    _mm_sfence();
    // apply profile
#ifdef _OPENMP
    #pragma omp parallel for schedule(static) default(none) shared(roi_in,roi_out, ivoid, ovoid)
#endif
    for(int j=0; j<roi_out->height; j++)
    {

      float *in  = (float*)ivoid + (size_t)ch*roi_in->width *j;
      float *out = (float*)ovoid + (size_t)ch*roi_out->width*j;

      for(int i=0; i<roi_out->width; i++, in+=ch, out+=ch )
      {
        for(int i=0; i<3; i++)
          if (d->lut[i][0] >= 0.0f)
          {
            out[i] = (out[i] < 1.0f) ? lerp_lut(d->lut[i], out[i]) : dt_iop_eval_exp(d->unbounded_coeffs[i], out[i]);
          }
      }
    }
  }
  else
  {
    //fprintf(stderr,"Using xform codepath\n");
    const __m128 outofgamutpixel = _mm_set_ps(0.0f, 1.0f, 1.0f, 0.0f);
#ifdef _OPENMP
    #pragma omp parallel for schedule(static) default(none) shared(ivoid, ovoid, roi_out)
#endif
    for (int k=0; k<roi_out->height; k++)
    {
      const float *in = ((float *)ivoid) + (size_t)ch*k*roi_out->width;
      float *out = ((float *)ovoid) + (size_t)ch*k*roi_out->width;

      if(!gamutcheck)
      {
        cmsDoTransform(d->xform, in, out, roi_out->width);
      } else {
        void *rgb = dt_alloc_align(16, 4*sizeof(float)*roi_out->width);
        cmsDoTransform(d->xform, in, rgb, roi_out->width);
        float *rgbptr = (float *)rgb;
        for (int j=0; j<roi_out->width; j++,rgbptr+=4,out+=4)
        {
          const __m128 pixel = _mm_load_ps(rgbptr);
          const __m128 ingamut = _mm_cmpge_ps(pixel, _mm_setzero_ps());
          const __m128 result = _mm_or_ps(_mm_andnot_ps(ingamut, outofgamutpixel),
                                          _mm_and_ps(ingamut, pixel));
          _mm_stream_ps(out, result);
        }
        dt_free_align(rgb);
      }
    }
    _mm_sfence();
  }

  if(piece->pipe->mask_display)
    dt_iop_alpha_copy(ivoid, ovoid, roi_out->width, roi_out->height);
}

static cmsHPROFILE _create_profile(gchar *iccprofile)
{
  cmsHPROFILE profile = NULL;
  if(!strcmp(iccprofile, "sRGB"))
  {
    // default: sRGB
    profile = dt_colorspaces_create_srgb_profile();
  }
  else if(!strcmp(iccprofile, "linear_rec709_rgb") || !strcmp(iccprofile, "linear_rgb"))
  {
    profile = dt_colorspaces_create_linear_rec709_rgb_profile();
  }
  else if(!strcmp(iccprofile, "linear_rec2020_rgb"))
  {
    profile = dt_colorspaces_create_linear_rec2020_rgb_profile();
  }
  else if(!strcmp(iccprofile, "adobergb"))
  {
    profile = dt_colorspaces_create_adobergb_profile();
  }
  else if(!strcmp(iccprofile, "X profile"))
  {
    // x default
    pthread_rwlock_rdlock(&darktable.control->xprofile_lock);
    if(darktable.control->xprofile_data)
      profile = cmsOpenProfileFromMem(darktable.control->xprofile_data, darktable.control->xprofile_size);
    pthread_rwlock_unlock(&darktable.control->xprofile_lock);
  }
  else
  {
    // else: load file name
    char filename[PATH_MAX];
    dt_colorspaces_find_profile(filename, sizeof(filename), iccprofile, "out");
    profile = cmsOpenProfileFromFile(filename, "r");
  }

  /* if no match lets fallback to srgb profile */
  if (!profile)
    profile = dt_colorspaces_create_srgb_profile();

  return profile;
}

void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)p1;
  dt_iop_colorout_data_t *d = (dt_iop_colorout_data_t *)piece->data;
  gchar *overprofile = dt_conf_get_string("plugins/lighttable/export/iccprofile");
  const int overintent = dt_conf_get_int("plugins/lighttable/export/iccintent");
  const int high_quality_processing = dt_conf_get_bool("plugins/lighttable/export/force_lcms2");
  gchar *outprofile=NULL;
  int outintent = 0;

  /* cleanup profiles */
  if (d->output)
    dt_colorspaces_cleanup_profile(d->output);
  d->output = NULL;

  if (d->softproof_enabled)
    dt_colorspaces_cleanup_profile(d->softproof);
  d->softproof = NULL;

  d->softproof_enabled = p->softproof_enabled;
  if(self->dev->gui_attached && self->gui_data != NULL)
  {
    dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
    g->softproof_enabled = p->softproof_enabled;
  }
  if(d->xform)
  {
    cmsDeleteTransform(d->xform);
    d->xform = NULL;
  }
  d->cmatrix[0] = NAN;
  d->lut[0][0] = -1.0f;
  d->lut[1][0] = -1.0f;
  d->lut[2][0] = -1.0f;
  piece->process_cl_ready = 1;

  /* if we are exporting then check and set usage of override profile */
  if (pipe->type == DT_DEV_PIXELPIPE_EXPORT)
  {
    if (overprofile && strcmp(overprofile, "image"))
      snprintf(p->iccprofile, DT_IOP_COLOR_ICC_LEN, "%s", overprofile);
    if (overintent >= 0)
      p->intent = overintent;

    outprofile = p->iccprofile;
    outintent = p->intent;
  }
  else
  {
    /* we are not exporting, using display profile as output */
    outprofile = p->displayprofile;
    outintent = p->displayintent;
  }

  /*
   * Setup transform flags
   */
  uint32_t transformFlags = 0;

  /* creating output profile */
  d->output = _create_profile(outprofile);

  /* creating softproof profile if softproof is enabled */
  if (d->softproof_enabled && pipe->type == DT_DEV_PIXELPIPE_FULL)
  {
    d->softproof =  _create_profile(p->softproofprofile);

    /* TODO: the use of bpc should be userconfigurable either from module or preference pane */
    /* softproof flag and black point compensation */
    transformFlags |= cmsFLAGS_SOFTPROOFING|cmsFLAGS_NOCACHE|cmsFLAGS_BLACKPOINTCOMPENSATION;

    if(d->softproof_enabled == DT_SOFTPROOF_GAMUTCHECK)
      transformFlags |= cmsFLAGS_GAMUTCHECK;
  }


  /* get matrix from profile, if softproofing or high quality exporting always go xform codepath */
  if (d->softproof_enabled || high_quality_processing ||
      dt_colorspaces_get_matrix_from_output_profile (d->output, d->cmatrix, d->lut[0], d->lut[1], d->lut[2], LUT_SAMPLES))
  {
    d->cmatrix[0] = NAN;
    piece->process_cl_ready = 0;
    d->xform = cmsCreateProofingTransform(d->Lab,
                                          TYPE_LabA_FLT,
                                          d->output,
                                          TYPE_RGBA_FLT,
                                          d->softproof,
                                          outintent,
                                          INTENT_RELATIVE_COLORIMETRIC,
                                          transformFlags);
  }

  // user selected a non-supported output profile, check that:
  if (!d->xform && isnan(d->cmatrix[0]))
  {
    dt_control_log(_("unsupported output profile has been replaced by sRGB!"));
    if (d->output)
      dt_colorspaces_cleanup_profile(d->output);
    d->output = dt_colorspaces_create_srgb_profile();
    if (d->softproof_enabled || dt_colorspaces_get_matrix_from_output_profile (d->output, d->cmatrix, d->lut[0], d->lut[1], d->lut[2], LUT_SAMPLES))
    {
      d->cmatrix[0] = NAN;
      piece->process_cl_ready = 0;

      d->xform = cmsCreateProofingTransform(d->Lab,
                                            TYPE_LabA_FLT,
                                            d->output,
                                            TYPE_RGBA_FLT,
                                            d->softproof,
                                            outintent,
                                            INTENT_RELATIVE_COLORIMETRIC,
                                            transformFlags);
    }
  }

  // now try to initialize unbounded mode:
  // we do extrapolation for input values above 1.0f.
  // unfortunately we can only do this if we got the computation
  // in our hands, i.e. for the fast builtin-dt-matrix-profile path.
  for(int k=0; k<3; k++)
  {
    // omit luts marked as linear (negative as marker)
    if(d->lut[k][0] >= 0.0f)
    {
      const float x[4] = {0.7f, 0.8f, 0.9f, 1.0f};
      const float y[4] = {lerp_lut(d->lut[k], x[0]),
                          lerp_lut(d->lut[k], x[1]),
                          lerp_lut(d->lut[k], x[2]),
                          lerp_lut(d->lut[k], x[3])
                         };
      dt_iop_estimate_exp(x, y, 4, d->unbounded_coeffs[k]);
    }
    else d->unbounded_coeffs[k][0] = -1.0f;
  }

  //fprintf(stderr, " Output profile %s, softproof %s%s%s\n", outprofile, d->softproof_enabled?"enabled ":"disabled",d->softproof_enabled?"using profile ":"",d->softproof_enabled?p->softproofprofile:"");

  g_free(overprofile);
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = malloc(sizeof(dt_iop_colorout_data_t));
  dt_iop_colorout_data_t *d = (dt_iop_colorout_data_t *)piece->data;
  d->softproof_enabled = 0;
  d->softproof = d->output = NULL;
  d->xform = NULL;
  d->Lab = dt_colorspaces_create_lab_profile();
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_colorout_data_t *d = (dt_iop_colorout_data_t *)piece->data;
  if(d->output) dt_colorspaces_cleanup_profile(d->output);
  dt_colorspaces_cleanup_profile(d->Lab);
  if(d->xform)
  {
    cmsDeleteTransform(d->xform);
    d->xform = NULL;
  }

  free(piece->data);
  piece->data = NULL;
}

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_module_t *module = (dt_iop_module_t *)self;
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
  dt_iop_colorout_params_t *p = (dt_iop_colorout_params_t *)module->params;
  dt_bauhaus_combobox_set(g->cbox1, (int)p->intent);
  dt_bauhaus_combobox_set(g->cbox4, (int)p->displayintent);

  int iccfound = 0, displayfound = 0, softprooffound = 0;
  GList *prof = g->profiles;
  while(prof)
  {
    dt_iop_color_profile_t *pp = (dt_iop_color_profile_t *)prof->data;
    if(!strcmp(pp->filename, p->iccprofile))
    {
      dt_bauhaus_combobox_set(g->cbox2, pp->pos);
      iccfound = 1;
    }
    if(!strcmp(pp->filename, p->displayprofile))
    {
      dt_bauhaus_combobox_set(g->cbox3, pp->display_pos);
      displayfound = 1;
    }
    if(!strcmp(pp->filename, p->softproofprofile))
    {
      dt_bauhaus_combobox_set(g->cbox5, pp->pos);
      softprooffound = 1;
    }

    if(iccfound && displayfound && softprooffound) break;
    prof = g_list_next(prof);
  }
  if(!iccfound)       dt_bauhaus_combobox_set(g->cbox2, 0);
  if(!displayfound)   dt_bauhaus_combobox_set(g->cbox3, 0);
  if(!softprooffound) dt_bauhaus_combobox_set(g->cbox5, 0);
  if(!iccfound)       fprintf(stderr, "[colorout] could not find requested profile `%s'!\n", p->iccprofile);
  if(!displayfound)   fprintf(stderr, "[colorout] could not find requested display profile `%s'!\n", p->displayprofile);
  if(!softprooffound) fprintf(stderr, "[colorout] could not find requested softproof profile `%s'!\n", p->softproofprofile);
}

void init(dt_iop_module_t *module)
{
  module->params = malloc(sizeof(dt_iop_colorout_params_t));
  module->default_params = malloc(sizeof(dt_iop_colorout_params_t));
  module->params_size = sizeof(dt_iop_colorout_params_t);
  module->gui_data = NULL;
  module->priority = 807; // module order created by iop_dependencies.py, do not edit!
  module->hide_enable_button = 1;
  module->default_enabled = 1;
  dt_iop_colorout_params_t tmp = (dt_iop_colorout_params_t)
  {"sRGB", "X profile", DT_INTENT_PERCEPTUAL, DT_INTENT_PERCEPTUAL,
    0, "sRGB",  DT_INTENT_PERCEPTUAL
  };
  memcpy(module->params, &tmp, sizeof(dt_iop_colorout_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_colorout_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

void gui_post_expose (struct dt_iop_module_t *self, cairo_t *cr, int32_t width, int32_t height, int32_t pointerx, int32_t pointery)
{
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
  if(g->softproof_enabled)
  {
    gchar *label= g->softproof_enabled == DT_SOFTPROOF_GAMUTCHECK ? _("gamut check") : _("soft proof");
    cairo_set_source_rgba(cr,0.5,0.5,0.5,0.5);
    cairo_text_extents_t te;
    cairo_select_font_face (cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, 20);
    cairo_text_extents (cr, label, &te);
    cairo_move_to (cr, te.height*2, height-(te.height*2));
    cairo_text_path (cr, label);
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 0.7);
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_stroke(cr);
  }

  const int high_quality_processing = dt_conf_get_bool("plugins/lighttable/export/force_lcms2");
  if (high_quality_processing)
  {
    gtk_widget_set_no_show_all(g->cbox1, FALSE);
    gtk_widget_set_visible(g->cbox1, TRUE);
    gtk_widget_set_no_show_all(g->cbox4, FALSE);
    gtk_widget_set_visible(g->cbox4, TRUE);
  }
  else
  {
    gtk_widget_set_no_show_all(g->cbox1, TRUE);
    gtk_widget_set_visible(g->cbox1, FALSE);
    gtk_widget_set_no_show_all(g->cbox4, TRUE);
    gtk_widget_set_visible(g->cbox4, FALSE);
  }
}

void gui_init(struct dt_iop_module_t *self)
{
  const int high_quality_processing = dt_conf_get_bool("plugins/lighttable/export/force_lcms2");

  self->gui_data = calloc(1, sizeof(dt_iop_colorout_gui_data_t));
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;

  g->profiles = NULL;
  dt_iop_color_profile_t *prof = (dt_iop_color_profile_t *)g_malloc0(sizeof(dt_iop_color_profile_t));
  g_strlcpy(prof->filename, "sRGB", sizeof(prof->filename));
  g_strlcpy(prof->name, "sRGB", sizeof(prof->name));
  int pos;
  int display_pos;
  prof->pos = 0;
  prof->display_pos = 0;
  g->profiles = g_list_append(g->profiles, prof);

  prof = (dt_iop_color_profile_t *)g_malloc0(sizeof(dt_iop_color_profile_t));
  g_strlcpy(prof->filename, "adobergb", sizeof(prof->filename));
  g_strlcpy(prof->name, "adobergb", sizeof(prof->name));
  prof->pos = 1;
  prof->display_pos = 1;
  g->profiles = g_list_append(g->profiles, prof);

  prof = (dt_iop_color_profile_t *)g_malloc0(sizeof(dt_iop_color_profile_t));
  g_strlcpy(prof->filename, "X profile", sizeof(prof->filename));
  g_strlcpy(prof->name, "X profile", sizeof(prof->name));
  prof->pos = -1;
  prof->display_pos = 2;
  g->profiles = g_list_append(g->profiles, prof);

  prof = (dt_iop_color_profile_t *)g_malloc0(sizeof(dt_iop_color_profile_t));
  g_strlcpy(prof->filename, "linear_rgb", sizeof(prof->filename));
  g_strlcpy(prof->name, "linear_rgb", sizeof(prof->name));
  pos = prof->pos = 2;
  display_pos = prof->display_pos = 3;
  g->profiles = g_list_append(g->profiles, prof);

  prof = (dt_iop_color_profile_t *)g_malloc0(sizeof(dt_iop_color_profile_t));
  g_strlcpy(prof->filename, "linear_rec2020_rgb", sizeof(prof->filename));
  g_strlcpy(prof->name, "linear_rec2020_rgb", sizeof(prof->name));
  pos = prof->pos = 3;
  display_pos = prof->display_pos = 4;
  g->profiles = g_list_append(g->profiles, prof);

  // read {conf,data}dir/color/out/*.icc
  char datadir[PATH_MAX];
  char confdir[PATH_MAX];
  char dirname[PATH_MAX];
  char filename[PATH_MAX];
  dt_loc_get_user_config_dir(confdir, sizeof(confdir));
  dt_loc_get_datadir(datadir, sizeof(datadir));
  snprintf(dirname, sizeof(dirname), "%s/color/out", confdir);
  if(!g_file_test(dirname, G_FILE_TEST_IS_DIR))
    snprintf(dirname, sizeof(dirname), "%s/color/out", datadir);
  cmsHPROFILE tmpprof;
  const gchar *d_name;
  GDir *dir = g_dir_open(dirname, 0, NULL);
  if(dir)
  {
    while((d_name = g_dir_read_name(dir)))
    {
      snprintf(filename, sizeof(filename), "%s/%s", dirname, d_name);
      tmpprof = cmsOpenProfileFromFile(filename, "r");
      if(tmpprof)
      {
        char *lang = getenv("LANG");
        if (!lang) lang = "en_US";

        dt_iop_color_profile_t *prof = (dt_iop_color_profile_t *)g_malloc0(sizeof(dt_iop_color_profile_t));
        dt_colorspaces_get_profile_name(tmpprof, lang, lang+3, prof->name, sizeof(prof->name));
        g_strlcpy(prof->filename, d_name, sizeof(prof->filename));
        prof->pos = ++pos;
        prof->display_pos = ++display_pos;
        cmsCloseProfile(tmpprof);
        g->profiles = g_list_append(g->profiles, prof);
      }
    }
    g_dir_close(dir);
  }

  self->widget = gtk_vbox_new(TRUE, DT_BAUHAUS_SPACE);

  // TODO:
  g->cbox1 = dt_bauhaus_combobox_new(self);
  gtk_box_pack_start(GTK_BOX(self->widget), g->cbox1, TRUE, TRUE, 0);
  dt_bauhaus_widget_set_label(g->cbox1, NULL, _("output intent"));
  dt_bauhaus_combobox_add(g->cbox1, _("perceptual"));
  dt_bauhaus_combobox_add(g->cbox1, _("relative colorimetric"));
  dt_bauhaus_combobox_add(g->cbox1, C_("rendering intent", "saturation"));
  dt_bauhaus_combobox_add(g->cbox1, _("absolute colorimetric"));
  g->cbox4 = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(g->cbox4, NULL, _("display intent"));
  gtk_box_pack_start(GTK_BOX(self->widget), g->cbox4, TRUE, TRUE, 0);
  dt_bauhaus_combobox_add(g->cbox4, _("perceptual"));
  dt_bauhaus_combobox_add(g->cbox4, _("relative colorimetric"));
  dt_bauhaus_combobox_add(g->cbox4, C_("rendering intent", "saturation"));
  dt_bauhaus_combobox_add(g->cbox4, _("absolute colorimetric"));

  if (!high_quality_processing)
  {
    gtk_widget_set_no_show_all(g->cbox1, TRUE);
    gtk_widget_set_visible(g->cbox1, FALSE);
    gtk_widget_set_no_show_all(g->cbox4, TRUE);
    gtk_widget_set_visible(g->cbox4, FALSE);
  }

  g->cbox2 = dt_bauhaus_combobox_new(self);
  g->cbox3 = dt_bauhaus_combobox_new(self);
  g->cbox5 = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(g->cbox2, NULL, _("output profile"));
  dt_bauhaus_widget_set_label(g->cbox5, NULL, _("softproof profile"));
  dt_bauhaus_widget_set_label(g->cbox3, NULL, _("display profile"));
  gtk_box_pack_start(GTK_BOX(self->widget), g->cbox2, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), g->cbox5, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), g->cbox3, TRUE, TRUE, 0);
  GList *l = g->profiles;
  while(l)
  {
    dt_iop_color_profile_t *prof = (dt_iop_color_profile_t *)l->data;
    if(!strcmp(prof->name, "X profile"))
    {
      // the system display profile is only suitable for display purposes
      dt_bauhaus_combobox_add(g->cbox3, _("system display profile"));
    }
    else if(!strcmp(prof->name, "linear_rec709_rgb") || !strcmp(prof->name, "linear_rgb"))
    {
      dt_bauhaus_combobox_add(g->cbox2, _("linear Rec709 RGB"));
      dt_bauhaus_combobox_add(g->cbox3, _("linear Rec709 RGB"));
      dt_bauhaus_combobox_add(g->cbox5, _("linear Rec709 RGB"));
    }
    else if(!strcmp(prof->name, "linear_rec2020_rgb"))
    {
      dt_bauhaus_combobox_add(g->cbox2, _("linear Rec2020 RGB"));
      dt_bauhaus_combobox_add(g->cbox3, _("linear Rec2020 RGB"));
      dt_bauhaus_combobox_add(g->cbox5, _("linear Rec2020 RGB"));
    }
    else if(!strcmp(prof->name, "sRGB"))
    {
      dt_bauhaus_combobox_add(g->cbox2, _("sRGB (web-safe)"));
      dt_bauhaus_combobox_add(g->cbox3, _("sRGB (web-safe)"));
      dt_bauhaus_combobox_add(g->cbox5, _("sRGB (web-safe)"));
    }
    else if(!strcmp(prof->name, "adobergb"))
    {
      dt_bauhaus_combobox_add(g->cbox2, _("Adobe RGB (compatible)"));
      dt_bauhaus_combobox_add(g->cbox3, _("Adobe RGB (compatible)"));
      dt_bauhaus_combobox_add(g->cbox5, _("Adobe RGB (compatible)"));
    }
    else
    {
      dt_bauhaus_combobox_add(g->cbox2, prof->name);
      dt_bauhaus_combobox_add(g->cbox3, prof->name);
      dt_bauhaus_combobox_add(g->cbox5, prof->name);
    }
    l = g_list_next(l);
  }

  char tooltip[1024];
  g_object_set(G_OBJECT(g->cbox1), "tooltip-text", _("rendering intent"), (char *)NULL);
  snprintf(tooltip, sizeof(tooltip), _("ICC profiles in %s/color/out or %s/color/out"), confdir, datadir);
  g_object_set(G_OBJECT(g->cbox2), "tooltip-text", tooltip, (char *)NULL);
  snprintf(tooltip, sizeof(tooltip), _("display ICC profiles in %s/color/out or %s/color/out"), confdir, datadir);
  g_object_set(G_OBJECT(g->cbox3), "tooltip-text", tooltip, (char *)NULL);
  snprintf(tooltip, sizeof(tooltip), _("softproof ICC profiles in %s/color/out or %s/color/out"), confdir, datadir);
  g_object_set(G_OBJECT(g->cbox5), "tooltip-text", tooltip, (char *)NULL);

  g_signal_connect (G_OBJECT (g->cbox1), "value-changed",
                    G_CALLBACK (intent_changed),
                    (gpointer)self);
  g_signal_connect (G_OBJECT (g->cbox4), "value-changed",
                    G_CALLBACK (display_intent_changed),
                    (gpointer)self);
  g_signal_connect (G_OBJECT (g->cbox2), "value-changed",
                    G_CALLBACK (output_profile_changed),
                    (gpointer)self);
  g_signal_connect (G_OBJECT (g->cbox3), "value-changed",
                    G_CALLBACK (display_profile_changed),
                    (gpointer)self);
  g_signal_connect (G_OBJECT (g->cbox5), "value-changed",
                    G_CALLBACK (softproof_profile_changed),
                    (gpointer)self);

  // reload the profiles when the display profile changed!
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_CONTROL_PROFILE_CHANGED,
                            G_CALLBACK(_signal_profile_changed), self->dev);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  dt_iop_colorout_gui_data_t *g = (dt_iop_colorout_gui_data_t *)self->gui_data;
  while(g->profiles)
  {
    g_free(g->profiles->data);
    g->profiles = g_list_delete_link(g->profiles, g->profiles);
  }
  free(self->gui_data);
  self->gui_data = NULL;
}

void init_key_accels(dt_iop_module_so_t *self)
{
  dt_accel_register_iop(self, FALSE, NC_("accel", "toggle softproofing"),
                        GDK_KEY_s, GDK_CONTROL_MASK);

  dt_accel_register_iop(self, FALSE, NC_("accel", "toggle gamutcheck"),
                        GDK_KEY_g, GDK_CONTROL_MASK);
}

void connect_key_accels(dt_iop_module_t *self)
{
  GClosure *closure;

  closure = g_cclosure_new(G_CALLBACK(key_softproof_callback),
                           (gpointer)self, NULL);
  dt_accel_connect_iop(self, "toggle softproofing", closure);

  closure = g_cclosure_new(G_CALLBACK(key_gamutcheck_callback),
                           (gpointer)self, NULL);
  dt_accel_connect_iop(self, "toggle gamutcheck", closure);

}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
