/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2009, Ithamar Adema, <ithamar.adema@team-embedded.nl>.
 * Distributed under the terms of the MIT License.
 */
#ifndef _UVC_CAM_DEVICE_H
#define _UVC_CAM_DEVICE_H


#include "CamDevice.h"
#include "UVCControls.h"
#include "USB_video.h"
#include <usb/USB_video.h>


class UVCCamDevice : public CamDevice {
public:
								UVCCamDevice(CamDeviceAddon &_addon,
									BUSBDevice* _device);
	virtual						~UVCCamDevice();

	virtual bool				SupportsIsochronous();
	virtual status_t			StartTransfer();
	virtual status_t			StopTransfer();
	virtual status_t			SuggestVideoFrame(uint32 &width,
									uint32 &height);
	virtual status_t			AcceptVideoFrame(uint32 &width,
									uint32 &height);
	virtual int32				CountVideoSizes() const;
	virtual bool				VideoSizeAt(int32 index, uint32 &width,
									uint32 &height, uint32 &interval) const;
	virtual void				AddParameters(BParameterGroup *group,
									int32 &index);
	virtual status_t			GetParameterValue(int32 id,
									bigtime_t *last_change, void *value,
									size_t *size);
	virtual status_t			SetParameterValue(int32 id, bigtime_t when,
									const void *value, size_t size);
	virtual status_t			FillFrameBuffer(BBuffer *buffer,
									bigtime_t *stamp = NULL);

private:
	virtual void				DataLost();
			void				_ParseVideoControl(
									const usbvc_class_descriptor* descriptor,
									size_t len);
			void				_ParseVideoStreaming(
									const usbvc_class_descriptor* descriptor,
									size_t len);
			status_t			_ProbeCommitFormat();
			status_t			_SelectBestAlternate();
			status_t			_ResetDeframer();
			uint32				_DefaultFrameInterval() const;
			status_t			_SelectIdleAlternate();

			void				_DefineControl(int32 parameter,
									UVCControlKind kind, uint8 selector,
									uint16 requestIndex, uint8 length,
									int32 autoParameter);
			void				_DefineControls();
			bool				_ReadControl(const UVCControl& control,
									uint8 request, uint8* data);
			void				_LoadControl(int32 parameter);
			void				_LoadWhiteBalanceComponent();
			status_t			_WriteControl(int32 parameter);
			void				_RefreshAutomaticValue(int32 parameter);
			bool				_IsAutomaticSwitch(int32 parameter) const;
			status_t			_WriteDependentValues(
									int32 automaticParameter);
			BParameterGroup*	_GroupFor(BParameterGroup* group,
									BParameterGroup** groups, int32 parameter);
			void				_MakeParameter(BParameterGroup* group,
									int32 parameter);

			usbvc_interface_header_descriptor *fHeaderDescriptor;

			const BUSBEndpoint*	fInterruptIn;
			uint32				fControlIndex;
			uint16				fControlRequestIndex;
			uint32				fStreamingIndex;
			uint32				fUncompressedFormatIndex;
			uint32				fUncompressedFrameIndex;
			uint32				fMJPEGFormatIndex;
			uint32				fMJPEGFrameIndex;
			uint32				fMaxVideoFrameSize;
			uint32				fMaxPayloadTransferSize;

			BList				fUncompressedFrames;
			BList				fMJPEGFrames;

			uint16				fCameraRequestIndex;
			uint8				fCameraControls;
			uint8				fProcessingControls[2];
			UVCControl			fControls[kUvcParameterCount];
};


class UVCCamDeviceAddon : public CamDeviceAddon {
public:
								UVCCamDeviceAddon(WebCamMediaAddOn* webcam);
	virtual 					~UVCCamDeviceAddon();

	virtual const char*			BrandName();
	virtual UVCCamDevice*		Instantiate(CamRoster &roster,
									BUSBDevice *from);
};

#endif /* _UVC_CAM_DEVICE_H */

