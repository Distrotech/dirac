
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <schroedinger/schrodebug.h>
#include <schroedinger/opengl/schroopengl.h>
#include <schroedinger/opengl/schroopenglcanvas.h>
#include <schroedinger/opengl/schroopenglframe.h>
#include <schroedinger/opengl/schroopenglshader.h>
#include <schroedinger/opengl/schroopenglshadercode.h>

void
schro_opengl_wavelet_transform (SchroFrameData *frame_data, int filter)
{
  SCHRO_ERROR ("unimplemented");
  SCHRO_ASSERT (0);
}

void
schro_opengl_wavelet_vertical_deinterleave (SchroFrameData *frame_data)
{
  int width, height;
  SchroOpenGLCanvas *canvas = NULL;
  SchroOpenGLCanvas *dest = NULL;
  SchroOpenGLCanvas *src = NULL;
  SchroOpenGLCanvas *temp = NULL;
  SchroOpenGLShader *shader_copy = NULL;
  SchroOpenGLShader *shader_vertical_deinterleave_l = NULL;
  SchroOpenGLShader *shader_vertical_deinterleave_h = NULL;

  SCHRO_ASSERT (SCHRO_FRAME_FORMAT_DEPTH (frame_data->format)
      == SCHRO_FRAME_FORMAT_DEPTH_S16);
  SCHRO_ASSERT (frame_data->width % 2 == 0);
  SCHRO_ASSERT (frame_data->height % 2 == 0);

  width = frame_data->width;
  height = frame_data->height;
  canvas = SCHRO_OPNEGL_CANVAS_FROM_FRAMEDATA (frame_data);

  SCHRO_ASSERT (canvas != NULL);

  schro_opengl_lock_context (canvas->opengl);

  shader_copy = schro_opengl_shader_get (canvas->opengl,
      SCHRO_OPENGL_SHADER_COPY_S16);
  shader_vertical_deinterleave_l = schro_opengl_shader_get (canvas->opengl,
      SCHRO_OPENGL_SHADER_IIWT_S16_VERTICAL_DEINTERLEAVE_L);
  shader_vertical_deinterleave_h = schro_opengl_shader_get (canvas->opengl,
      SCHRO_OPENGL_SHADER_IIWT_S16_VERTICAL_DEINTERLEAVE_H);

  SCHRO_ASSERT (shader_copy != NULL);
  SCHRO_ASSERT (shader_vertical_deinterleave_l != NULL);
  SCHRO_ASSERT (shader_vertical_deinterleave_h != NULL);

  schro_opengl_setup_viewport (width, height);

  SCHRO_OPENGL_CHECK_ERROR

  #define SWAP_DESTINATION_AND_SOURCE \
      temp = dest; dest = src; src = temp; \
      SCHRO_ASSERT (dest != src);

  #define BIND_FRAMEBUFFER_AND_TEXTURE \
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, dest->framebuffer); \
      schro_opengl_shader_bind_source (src->texture); \
      SCHRO_OPENGL_CHECK_ERROR

  dest = canvas->secondary;
  src = canvas;

  /* pass 1: vertical deinterleave */
  glUseProgramObjectARB (shader_vertical_deinterleave_l->program);

  BIND_FRAMEBUFFER_AND_TEXTURE

  schro_opengl_render_quad (0, 0, width, height / 2);

  glUseProgramObjectARB (shader_vertical_deinterleave_h->program);

  schro_opengl_shader_bind_offset (shader_vertical_deinterleave_h, 0,
      height / 2);

  schro_opengl_render_quad (0, height / 2, width, height / 2);

  SCHRO_OPENGL_CHECK_ERROR

  SCHRO_OPENGL_FLUSH

  /* pass 2: transfer data from secondary to primary framebuffer */
  glUseProgramObjectARB (shader_copy->program);

  SWAP_DESTINATION_AND_SOURCE
  BIND_FRAMEBUFFER_AND_TEXTURE

  schro_opengl_render_quad (0, 0, width, height);

  SCHRO_OPENGL_CHECK_ERROR

  SCHRO_OPENGL_FLUSH

  #undef SWAP_DESTINATION_AND_SOURCE
  #undef BIND_FRAMEBUFFER_AND_TEXTURE

#if SCHRO_OPENGL_UNBIND_TEXTURES
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
#endif
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  schro_opengl_unlock_context (canvas->opengl);
}

static void
schro_opengl_wavelet_render_quad (SchroOpenGLShader *shader, int x, int y,
    int quad_width, int quad_height, int total_width, int total_height)
{
  int inverse[] = { 0, 0 };
  int two[] = { 0, 0 }, one[] = { 0, 0 };

  inverse[0] = total_width - x - quad_width;
  inverse[1] = total_height - y - quad_height;

  if (quad_width == total_width && quad_height < total_height) {
    two[1] = 2;
    one[1] = 1;
  } else if (quad_width < total_width && quad_height == total_height) {
    two[0] = 2;
    one[0] = 1;
  } else {
    SCHRO_ERROR ("invalid quad to total relation");
    SCHRO_ASSERT (0);
  }

  SCHRO_ASSERT (inverse[0] >= 0);
  SCHRO_ASSERT (inverse[1] >= 0);

  #define UNIFORM(_operation, _number, _x, _y) \
      do { \
        if (shader->uniforms->_operation != -1) { \
          schro_opengl_shader_bind_##_operation (shader, \
              _x < _number[0] ? _x : _number[0], \
              _y < _number[1] ? _y : _number[1]); \
        } \
      } while (0)

  UNIFORM (decrease2, two, x, y);
  UNIFORM (decrease1, one, x, y);
  UNIFORM (increase1, one, inverse[0], inverse[1]);
  UNIFORM (increase2, two, inverse[0], inverse[1]);

  #undef UNIFORM

  schro_opengl_render_quad (x, y, quad_width, quad_height);
}

void
schro_opengl_wavelet_inverse_transform (SchroFrameData *frame_data,
    int filter)
{
  int width, height, subband_width, subband_height;
  int filter_shift = FALSE;
  SchroOpenGLCanvas *canvas = NULL;
  SchroOpenGLCanvas *dest = NULL;
  SchroOpenGLCanvas *src = NULL;
  SchroOpenGLCanvas *temp = NULL;
  SchroOpenGLShader *shader_copy = NULL;
  SchroOpenGLShader *shader_filter_lp = NULL;
  SchroOpenGLShader *shader_filter_hp = NULL;
  SchroOpenGLShader *shader_vertical_interleave = NULL;
  SchroOpenGLShader *shader_horizontal_interleave = NULL;
  SchroOpenGLShader *shader_filter_shift = NULL;

  SCHRO_ASSERT (SCHRO_FRAME_FORMAT_DEPTH (frame_data->format)
      == SCHRO_FRAME_FORMAT_DEPTH_S16);
  SCHRO_ASSERT (frame_data->width >= 2);
  SCHRO_ASSERT (frame_data->height >= 2);
  SCHRO_ASSERT (frame_data->width % 2 == 0);
  SCHRO_ASSERT (frame_data->height % 2 == 0);

  width = frame_data->width;
  height = frame_data->height;
  subband_width = width / 2;
  subband_height = height / 2;
  canvas = SCHRO_OPNEGL_CANVAS_FROM_FRAMEDATA (frame_data);

  SCHRO_ASSERT (canvas != NULL);

  schro_opengl_lock_context (canvas->opengl);

  shader_copy = schro_opengl_shader_get (canvas->opengl,
      SCHRO_OPENGL_SHADER_COPY_S16);

  SCHRO_ASSERT (shader_copy != NULL);

  switch (filter) {
    case SCHRO_WAVELET_DESLAURIES_DUBUC_9_7:
      shader_filter_lp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_DESLAURIERS_DUBUC_9_7_Lp);
      shader_filter_hp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_DESLAURIERS_DUBUC_9_7_Hp);

      filter_shift = TRUE;
      break;
    case SCHRO_WAVELET_LE_GALL_5_3:
      shader_filter_lp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_LE_GALL_5_3_Lp);
      shader_filter_hp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_LE_GALL_5_3_Hp);

      filter_shift = TRUE;
      break;
    case SCHRO_WAVELET_DESLAURIES_DUBUC_13_7:
      shader_filter_lp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_DESLAURIERS_DUBUC_13_7_Lp);
      shader_filter_hp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_DESLAURIERS_DUBUC_13_7_Hp);

      filter_shift = TRUE;
      break;
    case SCHRO_WAVELET_HAAR_0:
      shader_filter_lp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_HAAR_Lp);
      shader_filter_hp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_HAAR_Hp);

      filter_shift = FALSE;
      break;
    case SCHRO_WAVELET_HAAR_1:
      shader_filter_lp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_HAAR_Lp);
      shader_filter_hp = schro_opengl_shader_get (canvas->opengl,
          SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_HAAR_Hp);

      filter_shift = TRUE;
      break;
    case SCHRO_WAVELET_FIDELITY:
      SCHRO_ERROR ("fidelity filter is not implemented yet");
      SCHRO_ASSERT (0);

      filter_shift = FALSE;
      break;
    case SCHRO_WAVELET_DAUBECHIES_9_7:
      SCHRO_ERROR ("daubechies 9,7 filter is not implemented yet");
      SCHRO_ASSERT (0);

      filter_shift = TRUE;
      break;
    default:
      SCHRO_ERROR ("unknown filter %i", filter);
      SCHRO_ASSERT (0);
      break;
  }

  SCHRO_ASSERT (shader_filter_lp != NULL);
  SCHRO_ASSERT (shader_filter_hp != NULL);

  shader_vertical_interleave = schro_opengl_shader_get (canvas->opengl,
      SCHRO_OPENGL_SHADER_IIWT_S16_VERTICAL_INTERLEAVE);
  shader_horizontal_interleave = schro_opengl_shader_get (canvas->opengl,
      SCHRO_OPENGL_SHADER_IIWT_S16_HORIZONTAL_INTERLEAVE);

  SCHRO_ASSERT (shader_vertical_interleave != NULL);
  SCHRO_ASSERT (shader_horizontal_interleave != NULL);

  if (filter_shift) {
    shader_filter_shift = schro_opengl_shader_get (canvas->opengl,
        SCHRO_OPENGL_SHADER_IIWT_S16_FILTER_SHIFT);

    SCHRO_ASSERT (shader_filter_shift);
  } else {
    shader_filter_shift = NULL;
  }

  schro_opengl_setup_viewport (width, height);

  SCHRO_OPENGL_CHECK_ERROR

  #define SWAP_DESTINATION_AND_SOURCE \
      temp = dest; dest = src; src = temp; \
      SCHRO_ASSERT (dest != src);

  #define BIND_FRAMEBUFFER_AND_TEXTURE \
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, dest->framebuffer); \
      schro_opengl_shader_bind_source (src->texture); \
      SCHRO_OPENGL_CHECK_ERROR

  dest = canvas->secondary;
  src = canvas;

  /* pass 1: vertical filtering => XL + f(XH) = XL' */
  glUseProgramObjectARB (shader_filter_lp->program);

  BIND_FRAMEBUFFER_AND_TEXTURE

  schro_opengl_shader_bind_offset (shader_filter_lp, 0, subband_height);

  #define RENDER_QUAD_VERTICAL_Lp(_y, _quad_height) \
      schro_opengl_wavelet_render_quad (shader_filter_lp, 0, _y, width, \
          _quad_height, width, height)

  RENDER_QUAD_VERTICAL_Lp (0, 1);

  if (subband_height > 2) {
    RENDER_QUAD_VERTICAL_Lp (1, 1);

    if (subband_height > 4) {
      RENDER_QUAD_VERTICAL_Lp (2, subband_height - 4);
    }

    RENDER_QUAD_VERTICAL_Lp (subband_height - 2, 1);
  }

  RENDER_QUAD_VERTICAL_Lp (subband_height - 1, 1);

  #undef RENDER_QUAD_VERTICAL_Lp

  /* copy XH */
  glUseProgramObjectARB (shader_copy->program);

  schro_opengl_shader_bind_source (src->texture);

  schro_opengl_render_quad (0, subband_height, width, subband_height);

  SCHRO_OPENGL_CHECK_ERROR

  SCHRO_OPENGL_FLUSH

  /* pass 2: vertical filtering => f(XL') + XH = XH' */
  glUseProgramObjectARB (shader_filter_hp->program);

  SWAP_DESTINATION_AND_SOURCE
  BIND_FRAMEBUFFER_AND_TEXTURE

  schro_opengl_shader_bind_offset (shader_filter_hp, 0, subband_height);

  #define RENDER_QUAD_VERTICAL_Hp(_y_offset, _quad_height) \
      schro_opengl_wavelet_render_quad (shader_filter_hp, 0, \
          subband_height + (_y_offset), width, _quad_height, width, height)

  RENDER_QUAD_VERTICAL_Hp (0, 1);

  if (subband_height > 2) {
    RENDER_QUAD_VERTICAL_Hp (1, 1);

    if (subband_height > 4) {
      RENDER_QUAD_VERTICAL_Hp (2, subband_height - 4);
    }

    RENDER_QUAD_VERTICAL_Hp (subband_height - 2, 1);
  }

  RENDER_QUAD_VERTICAL_Hp (subband_height - 1, 1);

  #undef RENDER_QUAD_VERTICAL_Hp

  /* copy XL' */
  glUseProgramObjectARB (shader_copy->program);

  schro_opengl_shader_bind_source (src->texture);

  schro_opengl_render_quad (0, 0, width, subband_height);

  SCHRO_OPENGL_CHECK_ERROR

  SCHRO_OPENGL_FLUSH

  /* pass 3: vertical interleave => i(LL', LH') = L, i(HL', HH') = H */
  glUseProgramObjectARB (shader_vertical_interleave->program);

  SWAP_DESTINATION_AND_SOURCE
  BIND_FRAMEBUFFER_AND_TEXTURE

  schro_opengl_shader_bind_offset (shader_vertical_interleave, 0,
      subband_height);

  schro_opengl_render_quad (0, 0, width, height);

  SCHRO_OPENGL_CHECK_ERROR

  SCHRO_OPENGL_FLUSH

  /* pass 4: horizontal filtering => L + f(H) = L' */
  glUseProgramObjectARB (shader_filter_lp->program);

  SWAP_DESTINATION_AND_SOURCE
  BIND_FRAMEBUFFER_AND_TEXTURE

  schro_opengl_shader_bind_offset (shader_filter_lp, subband_width, 0);

  #define RENDER_QUAD_HORIZONTAL_Lp(_x, _quad_width) \
      schro_opengl_wavelet_render_quad (shader_filter_lp, _x, 0, _quad_width, \
          height, width, height)

  RENDER_QUAD_HORIZONTAL_Lp (0, 1);

  if (subband_width > 2) {
    RENDER_QUAD_HORIZONTAL_Lp (1, 1);

    if (subband_width > 4) {
      RENDER_QUAD_HORIZONTAL_Lp (2, subband_width - 4);
    }

    RENDER_QUAD_HORIZONTAL_Lp (subband_width - 2, 1);
  }

  RENDER_QUAD_HORIZONTAL_Lp (subband_width - 1, 1);

  #undef RENDER_QUAD_HORIZONTAL_Lp

  /* copy H */
  glUseProgramObjectARB (shader_copy->program);

  schro_opengl_shader_bind_source (src->texture);

  schro_opengl_render_quad (subband_width, 0, subband_width, height);

  SCHRO_OPENGL_CHECK_ERROR

  SCHRO_OPENGL_FLUSH

  /* pass 5: horizontal filtering => f(L') + H = H' */
  glUseProgramObjectARB (shader_filter_hp->program);

  SWAP_DESTINATION_AND_SOURCE
  BIND_FRAMEBUFFER_AND_TEXTURE

  schro_opengl_shader_bind_offset (shader_filter_hp, subband_width, 0);

  #define RENDER_QUAD_HORIZONTAL_Hp(_x_offset, _quad_width) \
      schro_opengl_wavelet_render_quad (shader_filter_hp, \
          subband_width + (_x_offset), 0, _quad_width, height, width, height);

  RENDER_QUAD_HORIZONTAL_Hp (0, 1);

  if (subband_width > 2) {
    RENDER_QUAD_HORIZONTAL_Hp (1, 1);

    if (subband_width > 4) {
      RENDER_QUAD_HORIZONTAL_Hp (2, subband_width - 4);
    }

    RENDER_QUAD_HORIZONTAL_Hp (subband_width - 2, 1);
  }

  RENDER_QUAD_HORIZONTAL_Hp (subband_width - 1, 1);

  #undef RENDER_QUAD_HORIZONTAL_Hp

  /* copy L' */
  glUseProgramObjectARB (shader_copy->program);

  schro_opengl_shader_bind_source (src->texture);

  schro_opengl_render_quad (0, 0, subband_width, height);

  SCHRO_OPENGL_CHECK_ERROR

  SCHRO_OPENGL_FLUSH

  /* pass 6: horizontal interleave => i(L', H') = LL */
  glUseProgramObjectARB (shader_horizontal_interleave->program);

  SWAP_DESTINATION_AND_SOURCE
  BIND_FRAMEBUFFER_AND_TEXTURE

  schro_opengl_shader_bind_offset (shader_horizontal_interleave, width / 2, 0);

  schro_opengl_render_quad (0, 0, width, height);

  SCHRO_OPENGL_CHECK_ERROR

  SCHRO_OPENGL_FLUSH

  /* pass 7: filter shift */
  if (filter_shift) {
    glUseProgramObjectARB (shader_filter_shift->program);

    SWAP_DESTINATION_AND_SOURCE
    BIND_FRAMEBUFFER_AND_TEXTURE

    schro_opengl_render_quad (0, 0, width, height);

    SCHRO_OPENGL_CHECK_ERROR
    SCHRO_OPENGL_FLUSH
  }

  /* pass 8: transfer data from secondary to primary framebuffer if previous
             pass result wasn't rendered into the primary framebuffer */
  if (dest != canvas) {
    glUseProgramObjectARB (shader_copy->program);

    SWAP_DESTINATION_AND_SOURCE
    BIND_FRAMEBUFFER_AND_TEXTURE

    schro_opengl_render_quad (0, 0, width, height);

    SCHRO_OPENGL_CHECK_ERROR
    SCHRO_OPENGL_FLUSH
  }

  #undef SWAP_DESTINATION_AND_SOURCE
  #undef BIND_FRAMEBUFFER_AND_TEXTURE

  glUseProgramObjectARB (0);
#if SCHRO_OPENGL_UNBIND_TEXTURES
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
#endif
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  schro_opengl_unlock_context (canvas->opengl);
}

