/*
 * cmd_test.c - unit test for VCMD command word builders (no hardware needed).
 * Verifies the opcode encoding matches the kernel's VCMD_READ_CMD layout.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "loongvpu_abi.h"
#include "vcmd.h"

int main(void)
{
	uint32_t buf[64];
	uint32_t *p = buf;

	/* WREG: opcode | (1<<16) | offset, then value */
	vcmd_cmd_wreg(&p, 0x0c, 0xdeadbeef);
	if (buf[0] != (OPCODE_WREG | (1u << 16) | 0x0c)) {
		fprintf(stderr, "WREG opcode word mismatch: %08x\n", buf[0]);
		return 1;
	}
	if (buf[1] != 0xdeadbeef) {
		fprintf(stderr, "WREG value mismatch\n");
		return 1;
	}

	/* END */
	p = buf;
	vcmd_cmd_end(&p);
	if (buf[0] != OPCODE_END) {
		fprintf(stderr, "END opcode mismatch\n");
		return 1;
	}

	/* JMP: opcode|IE, 0, 0, cmdbuf_id */
	p = buf;
	vcmd_cmd_jmp(&p, 0x1234);
	if (buf[0] != (OPCODE_JMP | JMP_IE_1) || buf[3] != 0x1234) {
		fprintf(stderr, "JMP encoding mismatch\n");
		return 1;
	}

	/* H.264 builder produces a well-formed stream ending with END. */
	memset(buf, 0, sizeof(buf));
	int words = vcmd_h264_build_decode_cmd(buf, 64, "dummy", 5, 0x1000, 0);
	if (words < 2) {
		fprintf(stderr, "h264 builder returned %d\n", words);
		return 1;
	}
	int has_end = 0;
	for (int i = 0; i < words; i++)
		if ((buf[i] & 0xf8000000u) == OPCODE_END) has_end = 1;
	if (!has_end) {
		fprintf(stderr, "h264 stream has no END command\n");
		return 1;
	}

	printf("cmd_test PASSED (h264 stream: %d words, ends with END)\n", words);
	return 0;
}
