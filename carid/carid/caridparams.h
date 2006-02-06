
#ifndef __CARID_PARAMS_H__
#define __CARID_PARAMS_H__

typedef struct _CaridParams CaridParams;
typedef struct _CaridSubband CaridSubband;

struct _CaridParams {

  int major_version;
  int minor_version;
  int profile;
  int level;

  int height;
  int width;

  /* */

  int chroma_format_index;
  int luma_offset;
  int luma_excursion;
  int chroma_offset;
  int chroma_excursion;
  int interlace;
  int top_field_first;
  int frame_rate_numerator;
  int frame_rate_denominator;
  int pixel_aspect_ratio_numerator;
  int pixel_aspect_ratio_denominator;
  int clean_tl_x;
  int clean_tl_y;
  int clean_width;
  int clean_height;
  int colour_matrix_index;
  int signal_range_index;
  int colour_primaries_index;
  int transfer_char_index;

  int non_spec_input;

  int have_chroma;
  int chroma_h_scale;
  int chroma_v_scale;
  int chroma_width;
  int chroma_height;
  int mc_chroma_width;
  int mc_chroma_height;
  int is_intra;

  /* transform parameters */
  int wavelet_filter_index;
  int transform_depth;
  int spatial_partition;
  int partition_index;
  int max_xblocks;
  int max_yblocks;
  int multi_quant;

  /* frame padding */
  int iwt_chroma_width;
  int iwt_chroma_height;
  int iwt_luma_width;
  int iwt_luma_height;

};

struct _CaridSubband {
  int x;
  int y;
  int w;
  int h;
  int offset;
  int stride;
  int chroma_w;
  int chroma_h;
  int chroma_offset;
  int chroma_stride;
  int has_parent;
  int scale_factor_shift;
  int horizontally_oriented;
  int vertically_oriented;
  int quant_index;
};

void carid_params_calculate_iwt_sizes (CaridParams *params);

void carid_params_set_video_format (CaridParams *params, int index);
int carid_params_get_video_format (CaridParams *params);
void carid_params_set_chroma_format (CaridParams *params, int index);
int carid_params_get_chroma_format (CaridParams *params);
void carid_params_set_frame_rate (CaridParams *params, int index);
int carid_params_get_frame_rate (CaridParams *params);
void carid_params_set_pixel_aspect_ratio (CaridParams *params, int index);
int carid_params_get_pixel_aspect_ratio (CaridParams *params);

#endif
