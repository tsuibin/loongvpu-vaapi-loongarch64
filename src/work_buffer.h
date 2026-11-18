#ifndef WORK_BUFFER_H
#define WORK_BUFFER_H

struct h264_decode_params;

/* Fill the VC8000D H.264 work buffer: CABAC init table @0, POC table @3680,
 * scaling lists @3816. `work` must be >= 4040 bytes (the RLC/VLC work buffer
 * allocated by ensure_work_buffers). */
void h264_fill_work_buffer(void *work, const struct h264_decode_params *prm);

/* Offsets/sizes, exported for tests. */
#define H264_WB_CABAC_SIZE 3680u
#define H264_WB_POC_OFFSET 3680u
#define H264_WB_POC_BYTES  136u
#define H264_WB_SCALE_OFFSET 3816u
#define H264_WB_SCALE_BYTES  224u
#define H264_WB_MIN_SIZE   4040u

#endif /* WORK_BUFFER_H */
