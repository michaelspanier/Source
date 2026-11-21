/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include "UVCDeframer.h"

#include "CamDebug.h"
#include "CamDevice.h"
#include "UVCPayload.h"

#include <Autolock.h>


#define MAX_TAG_LEN CAMDEFRAMER_MAX_TAG_LEN
static const int32 kMaxQueuedFrames = 2;


UVCDeframer::UVCDeframer(CamDevice* device)
	: CamDeframer(device),
	fFrameCount(0),
	fID(0),
	fSynced(false)
{
	uvc_sof_order_reset(&fSOFOrder);
}


UVCDeframer::~UVCDeframer()
{
}


ssize_t
UVCDeframer::Write(const void* buffer, size_t size)
{
	const uint8* buf = (const uint8*)buffer;

	uvc_payload_header header;
	if (!uvc_parse_payload_header(buf, size, &header))
		return size;

	// A changed frame ID means that the previous frame is complete.
	if (header.frameID != fID) {
		_FinishFrame();
		fID = header.frameID;
	}

	// Swapped transfers show as a step back of the camera's SOF counter.
	if (header.error
		|| (header.hasSOF && !uvc_sof_in_order(&fSOFOrder, header.sof)))
		fBuffer.MarkBroken();

	size_t payloadSize = size - header.length;
	fBuffer.Append(&buf[header.length], payloadSize);

	if (header.endOfFrame)
		_FinishFrame();

	return size;
}


/*!	Hands the collected payloads over as one frame and starts a new one.
	The first frame after the start is dropped, it usually begins in the middle,
	and so is a frame the camera marked as broken.
*/
void
UVCDeframer::_FinishFrame()
{
	if (!uvc_frame_usable(fBuffer.Length(), fBuffer.IsBroken())) {
		fBuffer.Clear();
		return;
	}

	if (!fSynced) {
		fSynced = true;
		fBuffer.Clear();
		return;
	}

	BAutolock l(fLocker);

	// A video stream is only useful live: when the consumer is behind, the
	// oldest frame goes, not the new one.
	while (fFrames.CountItems() >= kMaxQueuedFrames) {
		delete (CamFrame*)fFrames.RemoveItem((int32)0);
		acquire_sem_etc(fFrameSem, 1, B_RELATIVE_TIMEOUT, 0);
	}

	CamFrame* frame = AllocFrame();
	if (frame == NULL) {
		fBuffer.Clear();
		return;
	}
	frame->Write(fBuffer.Data(), fBuffer.Length());
	fFrames.AddItem(frame);
	fFrameCount++;
	release_sem(fFrameSem);
	fBuffer.Clear();
}


//!	Called like Write(), by one data pump at a time.
void
UVCDeframer::MarkFrameBroken()
{
	fBuffer.MarkBroken();
}


/*!	Starts a new stream: drops what is left of the last one and makes room
	for frames of up to \a maxFrameSize bytes. The data pumps must not run.
*/
status_t
UVCDeframer::Reset(size_t maxFrameSize)
{
	BAutolock l(fLocker);

	while (fFrames.CountItems() > 0) {
		delete (CamFrame*)fFrames.RemoveItem((int32)0);
		acquire_sem_etc(fFrameSem, 1, B_RELATIVE_TIMEOUT, 0);
	}
	fID = 0;
	fSynced = false;
	uvc_sof_order_reset(&fSOFOrder);
	return fBuffer.SetCapacity(maxFrameSize) ? B_OK : B_NO_MEMORY;
}


void
UVCDeframer::_PrintBuffer(const void* buffer, size_t size)
{
	uint8* b = (uint8*)buffer;
	for (size_t i = 0; i < size; i++)
		printf("0x%x\t", b[i]);
	printf("\n");
}

