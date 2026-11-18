#ifndef HEVC_WORK_BUFFER_H
#define HEVC_WORK_BUFFER_H

struct hevc_decode_params;

/* HEVC scaling-list work buffer size: ASIC_SCALING_LIST_SIZE = 16*64 = 1024. */
#define HEVC_WB_SCALE_BYTES 1024u

/* Pack HEVC scaling lists into the work buffer per the VC8000D SDK layout. */
void hevc_fill_scaling_list(uint8_t *dst, const struct hevc_decode_params *prm);

#endif /* HEVC_WORK_BUFFER_H */
