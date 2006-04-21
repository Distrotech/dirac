
#ifndef __SCHRO_DECODER_H__
#define __SCHRO_DECODER_H__

#include <schroedinger/schrobuffer.h>
#include <schroedinger/schroparams.h>
#include <schroedinger/schroframe.h>


typedef struct _SchroDecoder SchroDecoder;

struct _SchroDecoder {
  SchroFrame *frame;
  SchroFrame *mc_tmp_frame;
  SchroFrame *reference_frames[10];
  SchroFrame *output_frame;

  int16_t *tmpbuf;
  int16_t *tmpbuf2;

  int code;
  int rap_frame_number;
  int next_parse_offset;
  int prev_parse_offset;
  int frame_number_offset;

  SchroBits *bits;

  SchroParams params;

  SchroSubband subbands[1+6*3];

  SchroMotionVector *motion_vectors;
};

SchroDecoder * schro_decoder_new (void);
void schro_decoder_free (SchroDecoder *decoder);
void schro_decoder_set_output_frame (SchroDecoder *decoder, SchroFrame *frame);
int schro_decoder_is_parse_header (SchroBuffer *buffer);
int schro_decoder_is_rap (SchroBuffer *buffer);
void schro_decoder_decode (SchroDecoder *decoder, SchroBuffer *buffer);
void schro_decoder_decode_parse_header (SchroDecoder *decoder);
void schro_decoder_decode_rap (SchroDecoder *decoder);
void schro_decoder_decode_frame_header (SchroDecoder *decoder);
void schro_decoder_decode_frame_prediction (SchroDecoder *decoder);
void schro_decoder_decode_prediction_data (SchroDecoder *decoder);
void schro_decoder_decode_transform_parameters (SchroDecoder *decoder);
void schro_decoder_decode_transform_data (SchroDecoder *decoder, int component);
void schro_decoder_decode_subband (SchroDecoder *decoder, int component, int index);
void schro_decoder_iwt_transform (SchroDecoder *decoder, int component);
void schro_decoder_copy_from_frame_buffer (SchroDecoder *decoder, SchroBuffer *buffer);

#endif

