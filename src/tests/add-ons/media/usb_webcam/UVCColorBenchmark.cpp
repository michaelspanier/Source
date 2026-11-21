/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


//	Measures the YUY2 to B_RGB32 conversion: median of 20 runs.


#include "UVCColor.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


static const int kRuns = 20;


static double
now_seconds()
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return time.tv_sec + time.tv_nsec / 1e9;
}


static void
measure(int width, int height)
{
	size_t inSize = (size_t)width * height * 2;
	size_t outSize = (size_t)width * height * 4;
	uint8_t* in = (uint8_t*)malloc(inSize);
	uint8_t* out = (uint8_t*)malloc(outSize);
	if (in == NULL || out == NULL) {
		fprintf(stderr, "not enough memory for %dx%d\n", width, height);
		free(in);
		free(out);
		return;
	}
	for (size_t i = 0; i < inSize; i++)
		in[i] = (uint8_t)(i * 37 + 11);

	double decode[kRuns];
	double clear[kRuns];
	for (int run = 0; run < kRuns; run++) {
		double start = now_seconds();
		memset(out, 0, outSize);
		double middle = now_seconds();
		uvc_decode_yuy2(out, in, width, height);
		double end = now_seconds();
		clear[run] = middle - start;
		decode[run] = end - middle;
	}
	std::sort(decode, decode + kRuns);
	std::sort(clear, clear + kRuns);
	double decodeMedian = decode[kRuns / 2];
	double clearMedian = clear[kRuns / 2];
	printf("%4dx%-4d  Umrechnung %.2f ms (%.0f MB/s Eingang)   memset %.2f ms"
		"   Prüfsumme %d\n", width, height, decodeMedian * 1000,
		inSize / decodeMedian / 1e6, clearMedian * 1000, out[outSize / 3]);
	free(in);
	free(out);
}


int
main()
{
	measure(640, 480);
	measure(1280, 720);
	return 0;
}
