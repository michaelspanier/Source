/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2009, Ithamar Adema, <ithamar.adema@team-embedded.nl>.
 * Distributed under the terms of the MIT License.
 */


#include "UVCCamDevice.h"
#include "UVCColor.h"
#include "UVCControls.h"
#include "UVCDeframer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ParameterWeb.h>
#include <media/Buffer.h>


usb_webcam_support_descriptor kSupportedDevices[] = {
	// ofcourse we support a generic UVC device...
	{{ USB_VIDEO_DEVICE_CLASS, USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS, 0, 0, 0 }, "Generic UVC", "Video Class", "??" },
	{{ 0xEF, 0x02, 0, 0, 0 }, "Miscellaneous device", "Interface association", "??" },
	// ...whilst the following IDs were 'stolen' from a recent Linux driver:
	{{ 0, 0, 0, 0x045e, 0x00f8, }, "Microsoft",     "Lifecam NX-6000",                 "??" },
	{{ 0, 0, 0, 0x045e, 0x0723, }, "Microsoft",     "Lifecam VX-7000",                 "??" },
	{{ 0, 0, 0, 0x046d, 0x08c1, }, "Logitech",      "QuickCam Fusion",                 "??" },
	{{ 0, 0, 0, 0x046d, 0x08c2, }, "Logitech",      "QuickCam Orbit MP",               "??" },
	{{ 0, 0, 0, 0x046d, 0x08c3, }, "Logitech",      "QuickCam Pro for Notebook",       "??" },
	{{ 0, 0, 0, 0x046d, 0x08c5, }, "Logitech",      "QuickCam Pro 5000",               "??" },
	{{ 0, 0, 0, 0x046d, 0x08c6, }, "Logitech",      "QuickCam OEM Dell Notebook",      "??" },
	{{ 0, 0, 0, 0x046d, 0x08c7, }, "Logitech",      "QuickCam OEM Cisco VT Camera II", "??" },
	{{ 0, 0, 0, 0x046d, 0x0821, }, "Logitech",      "HD Pro Webcam C910",              "??" },
	{{ 0, 0, 0, 0x05ac, 0x8501, }, "Apple",         "Built-In iSight",                 "??" },
	{{ 0, 0, 0, 0x05e3, 0x0505, }, "Genesys Logic", "USB 2.0 PC Camera",               "??" },
	{{ 0, 0, 0, 0x0e8d, 0x0004, }, "N/A",           "MT6227",                          "??" },
	{{ 0, 0, 0, 0x174f, 0x5212, }, "Syntek",        "(HP Spartan)",                    "??" },
	{{ 0, 0, 0, 0x174f, 0x5931, }, "Syntek",        "(Samsung Q310)",                  "??" },
	{{ 0, 0, 0, 0x174f, 0x8a31, }, "Syntek",        "Asus F9SG",                       "??" },
	{{ 0, 0, 0, 0x174f, 0x8a33, }, "Syntek",        "Asus U3S",                        "??" },
	{{ 0, 0, 0, 0x17ef, 0x480b, }, "N/A",           "Lenovo Thinkpad SL500",           "??" },
	{{ 0, 0, 0, 0x18cd, 0xcafe, }, "Ecamm",         "Pico iMage",                      "??" },
	{{ 0, 0, 0, 0x19ab, 0x1000, }, "Bodelin",       "ProScopeHR",                      "??" },
	{{ 0, 0, 0, 0x1c4f, 0x3000, }, "SiGma Micro",   "USB Web Camera",                  "??" },
	{{ 0, 0, 0, 0, 0}, NULL, NULL, NULL }
};

/* Table 2-1 Compression Formats of USB Video Payload Uncompressed */
usbvc_guid kYUY2Guid = {0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
usbvc_guid kNV12Guid = {0x4e, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};

// The formats whose frames the device can show.
enum {
	kFormatNone = 0,
	kFormatYUY2,
	kFormatMJPEG
};

// A frame descriptor up to and including bFrameIntervalType.
static const size_t kFrameDescriptorLength = 26;
static const size_t kContinuousIntervalsLength = 12;
static const size_t kUncompressedFormatLength = 27;
static const size_t kMJPEGFormatLength = 11;


static void
print_guid(const usbvc_guid guid)
{
	if (!memcmp(guid, kYUY2Guid, sizeof(usbvc_guid)))
		printf("YUY2");
	else if (!memcmp(guid, kNV12Guid, sizeof(usbvc_guid)))
		printf("NV12");
	else {
		printf("%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
			"%02x:%02x:%02x:%02x", guid[0], guid[1], guid[2], guid[3], guid[4],
			guid[5], guid[6], guid[7], guid[8], guid[9], guid[10], guid[11],
			guid[12], guid[13], guid[14], guid[15]);
	}
}


UVCCamDevice::UVCCamDevice(CamDeviceAddon& _addon, BUSBDevice* _device)
	: CamDevice(_addon, _device),
	fHeaderDescriptor(NULL),
	fInterruptIn(NULL),
	fParsedFormatIndex(0),
	fParsedFormat(kFormatNone)
{
	uvc_frame_list_init(&fFrames);
	memset(&fFrame, 0, sizeof(fFrame));
	fControlRequestIndex = 0;
	fCameraRequestIndex = 0;
	fCameraControls = 0;
	fProcessingControls[0] = 0;
	fProcessingControls[1] = 0;
	for (int32 i = 0; i < kUvcParameterCount; i++)
		fControls[i].kind = kUvcControlNone;

	fDeframer = new UVCDeframer(this);
	SetDataInput(fDeframer);

	const BUSBConfiguration* config;
	const BUSBInterface* interface;
	usb_descriptor* generic;
	uint8 buffer[1024];

	generic = (usb_descriptor*)buffer;

	bool streamingFound = false;
	for (uint32 i = 0; i < _device->CountConfigurations(); i++) {
		config = _device->ConfigurationAt(i);
		if (config == NULL)
			continue;
		_device->SetConfiguration(config);
		for (uint32 j = 0; j < config->CountInterfaces(); j++) {
			interface = config->InterfaceAt(j);
			if (interface == NULL)
				continue;

			if (interface->Class() == USB_VIDEO_DEVICE_CLASS && interface->Subclass()
				== USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS) {
				printf("UVCCamDevice: (%" B_PRIu32 ",%" B_PRIu32 "): Found Video Control "
					"interface.\n", i, j);

				// look for class specific interface descriptors and parse them
				for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
					sizeof(buffer)) == B_OK; k++) {
					if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
						| USB_DESCRIPTOR_INTERFACE))
						continue;
					fControlIndex = interface->Index();
					_ParseVideoControl((const usbvc_class_descriptor*)generic,
						generic->generic.length);
				}
				for (uint32 k = 0; k < interface->CountEndpoints(); k++) {
					const BUSBEndpoint* e = interface->EndpointAt(k);
					if (e && e->IsInterrupt() && e->IsInput()) {
						fInterruptIn = e;
						break;
					}
				}
				fInitStatus = B_OK;
			} else if (interface->Class() == USB_VIDEO_DEVICE_CLASS && interface->Subclass()
				== USB_VIDEO_INTERFACE_VIDEOSTREAMING_SUBCLASS) {
				printf("UVCCamDevice: (%" B_PRIu32 ",%" B_PRIu32 "): Found Video Streaming "
					"interface.\n", i, j);

				// Format and frame indices only hold within their interface,
				// and the stream is set up on one. A second one, as of an
				// infrared camera, is left alone.
				if (streamingFound)
					continue;
				streamingFound = true;

				// An earlier run may have left an alternate setting active,
				// whose interface has no class specific descriptors. Parse
				// and probe from the idle setting.
				if (interface->ActiveAlternateIndex() != 0)
					((BUSBInterface*)interface)->SetAlternate(0);

				// look for class specific interface descriptors and parse them
				for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
					sizeof(buffer)) == B_OK; k++) {
					if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
						| USB_DESCRIPTOR_INTERFACE))
						continue;
					fStreamingIndex = interface->Index();
					_ParseVideoStreaming((const usbvc_class_descriptor*)generic,
						generic->generic.length);
				}

				for (uint32 k = 0; k < interface->CountEndpoints(); k++) {
					const BUSBEndpoint* e = interface->EndpointAt(k);
					if (e && e->IsIsochronous() && e->IsInput()) {
						fIsoIn = e;
						break;
					}
				}
			}
		}
	}
}


UVCCamDevice::~UVCCamDevice()
{
	free(fHeaderDescriptor);
}


void
UVCCamDevice::_ParseVideoStreaming(const usbvc_class_descriptor* _descriptor,
	size_t len)
{
	switch (_descriptor->descriptorSubtype) {
		case USB_VIDEO_VS_INPUT_HEADER:
		{
			const usb_video_class_specific_vs_interface_input_header_descriptor* descriptor
				= (const usb_video_class_specific_vs_interface_input_header_descriptor*)_descriptor;
			printf("VS_INPUT_HEADER:\t#fmts=%d,ept=0x%x (%s)\n", descriptor->num_formats,
				descriptor->_endpoint_address.endpoint_number,
				descriptor->_endpoint_address.direction ? "IN" : "OUT");
			if (descriptor->_info.dynamic_format_change_support)
				printf("\tDynamic Format Change supported\n");
			printf("\toutput terminal id=%d\n", descriptor->terminal_link);
			printf("\tstill capture method=%d\n", descriptor->still_capture_method);
			if (descriptor->trigger_support) {
				printf("\ttrigger button fixed to still capture=%s\n",
					descriptor->trigger_usage ? "no" : "yes");
			}
			const struct usb_video_class_specific_vs_interface_input_header_descriptor::ma_controls*
				controls = descriptor->_ma_controls;
			for (uint8 i = 0; i < descriptor->num_formats; i++,
				controls =
				(const struct usb_video_class_specific_vs_interface_input_header_descriptor
					::ma_controls*)((const char*)controls + descriptor->control_size)) {
				printf("\tfmt%d: %s %s %s %s - %s %s\n", i,
					(controls->key_frame_rate) ? "wKeyFrameRate" : "",
					(controls->p_frame_rate) ? "wPFrameRate" : "",
					(controls->comp_quality) ? "wCompQuality" : "",
					(controls->comp_window_size) ? "wCompWindowSize" : "",
					(controls->generate_key_frame) ? "<Generate Key Frame>" : "",
					(controls->update_frame_segment) ? "<Update Frame Segment>" : "");
			}
			break;
		}
		case USB_VIDEO_VS_FORMAT_UNCOMPRESSED:
		{
			const usbvc_format_descriptor* descriptor
				= (const usbvc_format_descriptor*)_descriptor;
			if (len < kUncompressedFormatLength) {
				fParsedFormat = kFormatNone;
				break;
			}
			fParsedFormatIndex = descriptor->formatIndex;
			fParsedFormat = memcmp(descriptor->uncompressed.format, kYUY2Guid,
				sizeof(usbvc_guid)) == 0 ? kFormatYUY2 : kFormatNone;
			printf("VS_FORMAT_UNCOMPRESSED:\tbFormatIdx=%d,#frmdesc=%d,guid=",
				descriptor->formatIndex, descriptor->numFrameDescriptors);
			print_guid(descriptor->uncompressed.format);
			printf("\n\t#bpp=%d,optfrmidx=%d,aspRX=%d,aspRY=%d\n",
				descriptor->uncompressed.bytesPerPixel,
				descriptor->uncompressed.defaultFrameIndex,
				descriptor->uncompressed.aspectRatioX,
				descriptor->uncompressed.aspectRatioY);
			printf("\tbmInterlaceFlags:\n");
			if (descriptor->uncompressed.interlaceFlags & 1)
				printf("\tInterlaced stream or variable\n");
			printf("\t%d fields per frame\n",
				(descriptor->uncompressed.interlaceFlags & 2) ? 1 : 2);
			if (descriptor->uncompressed.interlaceFlags & 4)
				printf("\tField 1 first\n");
			printf("\tField Pattern: ");
			switch ((descriptor->uncompressed.interlaceFlags & 0x30) >> 4) {
				case 0: printf("Field 1 only\n"); break;
				case 1: printf("Field 2 only\n"); break;
				case 2: printf("Regular pattern of fields 1 and 2\n"); break;
				case 3: printf("Random pattern of fields 1 and 2\n"); break;
			}
			if (descriptor->uncompressed.copyProtect)
				printf("\tRestrict duplication\n");
			break;
		}
		case USB_VIDEO_VS_FRAME_MJPEG:
		case USB_VIDEO_VS_FRAME_UNCOMPRESSED:
		{
			const usb_video_frame_descriptor* descriptor
				= (const usb_video_frame_descriptor*)_descriptor;
			if (len < kFrameDescriptorLength)
				break;
			const bool compressed
				= _descriptor->descriptorSubtype == USB_VIDEO_VS_FRAME_MJPEG;
			printf("%s", compressed ? "VS_FRAME_MJPEG:" : "VS_FRAME_UNCOMPRESSED:");
			if (fParsedFormat == (compressed ? kFormatMJPEG : kFormatYUY2))
				_AddFrame(*descriptor, compressed);
			printf("\tbFrameIdx=%d,stillsupported=%s,"
				"fixedframerate=%s\n", descriptor->frame_index,
				(descriptor->capabilities & 1) ? "yes" : "no",
				(descriptor->capabilities & 2) ? "yes" : "no");
			printf("\twidth=%u,height=%u,min/max bitrate=%" B_PRIu32 "/%" B_PRIu32 ", maxbuf=%" B_PRIu32 "\n",
				descriptor->width, descriptor->height,
				descriptor->min_bit_rate, descriptor->max_bit_rate,
				descriptor->max_video_frame_buffer_size);
			printf("\tdefault frame interval: %" B_PRIu32 ", #intervals(0=cont): %d\n",
				descriptor->default_frame_interval, descriptor->frame_interval_type);
			if (descriptor->frame_interval_type == 0
				&& len >= kFrameDescriptorLength + kContinuousIntervalsLength) {
				printf("min/max frame interval=%" B_PRIu32 "/%" B_PRIu32 ", step=%" B_PRIu32 "\n",
					descriptor->continuous.min_frame_interval,
					descriptor->continuous.max_frame_interval,
					descriptor->continuous.frame_interval_step);
			} else for (uint8 i = 0; i < descriptor->frame_interval_type
				&& kFrameDescriptorLength + (i + 1) * sizeof(uint32) <= len;
				i++) {
				printf("\tdiscrete frame interval: %" B_PRIu32 "\n",
					descriptor->discrete_frame_intervals[i]);
			}
			break;
		}
		case USB_VIDEO_VS_COLORFORMAT:
		{
			const usb_video_color_matching_descriptor* descriptor
				= (const usb_video_color_matching_descriptor*)_descriptor;
			printf("VS_COLORFORMAT:\n\tbColorPrimaries: ");
			switch (descriptor->color_primaries) {
				case 0: printf("Unspecified\n"); break;
				case 1: printf("BT.709,sRGB\n"); break;
				case 2: printf("BT.470-2(M)\n"); break;
				case 3: printf("BT.470-2(B,G)\n"); break;
				case 4: printf("SMPTE 170M\n"); break;
				case 5: printf("SMPTE 240M\n"); break;
				default: printf("Invalid (%d)\n", descriptor->color_primaries);
			}
			printf("\tbTransferCharacteristics: ");
			switch (descriptor->transfer_characteristics) {
				case 0: printf("Unspecified\n"); break;
				case 1: printf("BT.709\n"); break;
				case 2: printf("BT.470-2(M)\n"); break;
				case 3: printf("BT.470-2(B,G)\n"); break;
				case 4: printf("SMPTE 170M\n"); break;
				case 5: printf("SMPTE 240M\n"); break;
				case 6: printf("Linear (V=Lc)\n"); break;
				case 7: printf("sRGB\n"); break;
				default: printf("Invalid (%d)\n",
					descriptor->transfer_characteristics);
			}
			printf("\tbMatrixCoefficients: ");
			switch (descriptor->matrix_coefficients) {
				case 0: printf("Unspecified\n"); break;
				case 1: printf("BT.709\n"); break;
				case 2: printf("FCC\n"); break;
				case 3: printf("BT.470-2(B,G)\n"); break;
				case 4: printf("SMPTE 170M (BT.601)\n"); break;
				case 5: printf("SMPTE 240M\n"); break;
				default: printf("Invalid (%d)\n", descriptor->matrix_coefficients);
			}
			break;
		}
		case USB_VIDEO_VS_OUTPUT_HEADER:
		{
			const usb_video_class_specific_vs_interface_output_header_descriptor* descriptor
				= (const usb_video_class_specific_vs_interface_output_header_descriptor*)_descriptor;
			printf("VS_OUTPUT_HEADER:\t#fmts=%d,ept=0x%x (%s)\n",
				descriptor->num_formats, descriptor->_endpoint_address.endpoint_number,
				descriptor->_endpoint_address.direction ? "IN" : "OUT");
			printf("\toutput terminal id=%d\n", descriptor->terminal_link);
			const struct usb_video_class_specific_vs_interface_output_header_descriptor::ma_controls*
				controls = descriptor->_ma_controls;
			for (uint8 i = 0; i < descriptor->num_formats; i++,
				controls
					= (const struct usb_video_class_specific_vs_interface_output_header_descriptor
					::ma_controls*)((const char*)controls + descriptor->control_size)) {
				printf("\tfmt%d: %s %s %s %s\n", i,
					(controls->key_frame_rate) ? "wKeyFrameRate" : "",
					(controls->p_frame_rate) ? "wPFrameRate" : "",
					(controls->comp_quality) ? "wCompQuality" : "",
					(controls->comp_window_size) ? "wCompWindowSize" : "");
			}
			break;
		}
		case USB_VIDEO_VS_STILL_IMAGE_FRAME:
		{
			const usb_video_still_image_frame_descriptor* descriptor
				= (const usb_video_still_image_frame_descriptor*)_descriptor;
			printf("VS_STILL_IMAGE_FRAME:\t#imageSizes=%d,compressions=%d,"
				"ept=0x%x\n", descriptor->num_image_size_patterns,
				descriptor->NumCompressionPatterns(),
				descriptor->endpoint_address);
			for (uint8 i = 0; i < descriptor->num_image_size_patterns; i++) {
				printf("imageSize%d: %dx%d\n", i,
					descriptor->_pattern_size[i].width,
					descriptor->_pattern_size[i].height);
			}
			for (uint8 i = 0; i < descriptor->NumCompressionPatterns(); i++) {
				printf("compression%d: %d\n", i,
					descriptor->CompressionPatterns()[i]);
			}
			break;
		}
		case USB_VIDEO_VS_FORMAT_MJPEG:
		{
			const usbvc_format_descriptor* descriptor
				= (const usbvc_format_descriptor*)_descriptor;
			if (len < kMJPEGFormatLength) {
				fParsedFormat = kFormatNone;
				break;
			}
			fParsedFormatIndex = descriptor->formatIndex;
			fParsedFormat = kFormatMJPEG;
			printf("VS_FORMAT_MJPEG:\tbFormatIdx=%d,#frmdesc=%d\n",
				descriptor->formatIndex, descriptor->numFrameDescriptors);
			printf("\t#flgs=%d,optfrmidx=%d,aspRX=%d,aspRY=%d\n",
				descriptor->mjpeg.flags,
				descriptor->mjpeg.defaultFrameIndex,
				descriptor->mjpeg.aspectRatioX,
				descriptor->mjpeg.aspectRatioY);
			printf("\tbmInterlaceFlags:\n");
			if (descriptor->mjpeg.interlaceFlags & 1)
				printf("\tInterlaced stream or variable\n");
			printf("\t%d fields per frame\n",
				(descriptor->mjpeg.interlaceFlags & 2) ? 1 : 2);
			if (descriptor->mjpeg.interlaceFlags & 4)
				printf("\tField 1 first\n");
			printf("\tField Pattern: ");
			switch ((descriptor->mjpeg.interlaceFlags & 0x30) >> 4) {
				case 0: printf("Field 1 only\n"); break;
				case 1: printf("Field 2 only\n"); break;
				case 2: printf("Regular pattern of fields 1 and 2\n"); break;
				case 3: printf("Random pattern of fields 1 and 2\n"); break;
			}
			if (descriptor->mjpeg.copyProtect)
				printf("\tRestrict duplication\n");
			break;
		}
		case USB_VIDEO_VS_FORMAT_MPEG2TS:
			printf("VS_FORMAT_MPEG2TS:\t\n");
			break;
		case USB_VIDEO_VS_FORMAT_DV:
			printf("VS_FORMAT_DV:\t\n");
			break;
		case USB_VIDEO_VS_FORMAT_FRAME_BASED:
			printf("VS_FORMAT_FRAME_BASED:\t\n");
			break;
		case USB_VIDEO_VS_FRAME_FRAME_BASED:
			printf("VS_FRAME_FRAME_BASED:\t\n");
			break;
		case USB_VIDEO_VS_FORMAT_STREAM_BASED:
			printf("VS_FORMAT_STREAM_BASED:\t\n");
			break;
		default:
			printf("INVALID STREAM UNIT TYPE=%d!\n",
				_descriptor->descriptorSubtype);
	}
}


void
UVCCamDevice::_ParseVideoControl(const usbvc_class_descriptor* _descriptor,
	size_t len)
{
	switch (_descriptor->descriptorSubtype) {
		case USB_VIDEO_VC_HEADER:
		{
			if (fHeaderDescriptor != NULL) {
				printf("ERROR: multiple VC_HEADER! Skipping...\n");
				break;
			}
			fHeaderDescriptor = (usbvc_interface_header_descriptor*)malloc(len);
			memcpy(fHeaderDescriptor, _descriptor, len);
			printf("VC_HEADER:\tUVC v%x.%02x, clk %.5f MHz\n",
				fHeaderDescriptor->version >> 8,
				fHeaderDescriptor->version & 0xff,
				fHeaderDescriptor->clockFrequency / 1000000.0);
			for (uint8 i = 0; i < fHeaderDescriptor->numInterfacesNumbers; i++) {
				printf("\tStreaming Interface %d\n",
					fHeaderDescriptor->interfaceNumbers[i]);
			}
			break;
		}
		case USB_VIDEO_VC_INPUT_TERMINAL:
		{
			const usbvc_input_terminal_descriptor* descriptor
				= (const usbvc_input_terminal_descriptor*)_descriptor;
			printf("VC_INPUT_TERMINAL:\tid=%d,type=%04x,associated terminal="
				"%d\n", descriptor->terminalID, descriptor->terminalType,
				descriptor->associatedTerminal);
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->terminal));
			if (descriptor->terminalType == 0x201) {
				const usb_video_camera_terminal_descriptor* desc
					= (const usb_video_camera_terminal_descriptor*)descriptor;
				fCameraRequestIndex = fControlIndex + (descriptor->terminalID << 8);
				if (len > 15 && desc->control_size >= 1)
					fCameraControls = ((const uint8*)descriptor)[15];
				printf("\tObjectiveFocalLength Min/Max %d/%d\n",
					desc->objective_focal_length_min,
					desc->objective_focal_length_max);
				printf("\tOcularFocalLength %d\n", desc->ocular_focal_length);
				printf("\tControlSize %d\n", desc->control_size);
			}
			break;
		}
		case USB_VIDEO_VC_OUTPUT_TERMINAL:
		{
			const usb_video_output_terminal_descriptor* descriptor
				= (const usb_video_output_terminal_descriptor*)_descriptor;
			printf("VC_OUTPUT_TERMINAL:\tid=%d,type=%04x,associated terminal="
				"%d, src id=%d\n", descriptor->terminal_id,
				descriptor->terminal_type, descriptor->associated_terminal,
				descriptor->source_id);
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->terminal));
			break;
		}
		case USB_VIDEO_VC_SELECTOR_UNIT:
		{
			const usb_video_selector_unit_descriptor* descriptor
				= (const usb_video_selector_unit_descriptor*)_descriptor;
			printf("VC_SELECTOR_UNIT:\tid=%d,#pins=%d\n",
				descriptor->unit_id, descriptor->num_input_pins);
			printf("\t");
			for (uint8 i = 0; i < descriptor->num_input_pins; i++)
				printf("%d ", descriptor->source_id[i]);
			printf("\n");
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->Selector()));
			break;
		}
		case USB_VIDEO_VC_PROCESSING_UNIT:
		{
			const usb_video_processing_unit_descriptor* descriptor
				= (const usb_video_processing_unit_descriptor*)_descriptor;
			fControlRequestIndex = fControlIndex + (descriptor->unit_id << 8);
			for (uint8 i = 0; i < 2; i++) {
				if (i < descriptor->control_size)
					fProcessingControls[i] = descriptor->controls[i];
			}
			printf("VC_PROCESSING_UNIT:\t unit id=%d,src id=%d, digmul=%d\n",
				descriptor->unit_id, descriptor->source_id,
				descriptor->max_multiplier);
			printf("\tbControlSize=%d\n", descriptor->control_size);
			if (descriptor->control_size >= 1) {
				if (descriptor->controls[0] & 1)
					printf("\tBrightness\n");
				if (descriptor->controls[0] & 2)
					printf("\tContrast\n");
				if (descriptor->controls[0] & 4)
					printf("\tHue\n");
				if (descriptor->controls[0] & 8)
					printf("\tSaturation\n");
				if (descriptor->controls[0] & 16)
					printf("\tSharpness\n");
				if (descriptor->controls[0] & 32)
					printf("\tGamma\n");
				if (descriptor->controls[0] & 64)
					printf("\tWhite Balance Temperature\n");
				if (descriptor->controls[0] & 128)
					printf("\tWhite Balance Component\n");
			}
			if (descriptor->control_size >= 2) {
				if (descriptor->controls[1] & 1)
					printf("\tBacklight Compensation\n");
				if (descriptor->controls[1] & 2)
					printf("\tGain\n");
				if (descriptor->controls[1] & 4)
					printf("\tPower Line Frequency\n");
				if (descriptor->controls[1] & 8)
					printf("\t[AUTO] Hue\n");
				if (descriptor->controls[1] & 16)
					printf("\t[AUTO] White Balance Temperature\n");
				if (descriptor->controls[1] & 32)
					printf("\t[AUTO] White Balance Component\n");
				if (descriptor->controls[1] & 64)
					printf("\tDigital Multiplier\n");
				if (descriptor->controls[1] & 128)
					printf("\tDigital Multiplier Limit\n");
			}
			if (descriptor->control_size >= 3) {
				if (descriptor->controls[2] & 1)
					printf("\tAnalog Video Standard\n");
				if (descriptor->controls[2] & 2)
					printf("\tAnalog Video Lock Status\n");
			}
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->Processing()));
			if (descriptor->VideoStandards()._video_standards.ntsc_525_60)
				printf("\tNTSC  525/60\n");
			if (descriptor->VideoStandards()._video_standards.pal_625_50)
				printf("\tPAL   625/50\n");
			if (descriptor->VideoStandards()._video_standards.secam_625_50)
				printf("\tSECAM 625/50\n");
			if (descriptor->VideoStandards()._video_standards.ntsc_625_50)
				printf("\tNTSC  625/50\n");
			if (descriptor->VideoStandards()._video_standards.pal_525_60)
				printf("\tPAL   525/60\n");
			break;
		}
		case USB_VIDEO_VC_EXTENSION_UNIT:
		{
			const usb_video_extension_unit_descriptor* descriptor
				= (const usb_video_extension_unit_descriptor*)_descriptor;
			printf("VC_EXTENSION_UNIT:\tid=%d, guid=", descriptor->unit_id);
			print_guid(descriptor->guid_extension_code);
			printf("\n\t#ctrls=%d, #pins=%d\n", descriptor->num_controls,
				descriptor->num_input_pins);
			printf("\t");
			for (uint8 i = 0; i < descriptor->num_input_pins; i++)
				printf("%d ", descriptor->source_id[i]);
			printf("\n");
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->Extension()));
			break;
		}
		default:
			printf("Unknown control %d\n", _descriptor->descriptorSubtype);
	}
}


bool
UVCCamDevice::SupportsIsochronous()
{
	return true;
}


status_t
UVCCamDevice::StartTransfer()
{
	if (fFrame.compressed && !fJpegDecoder.InitCheck())
		return B_NO_MEMORY;
	if (_ProbeCommitFormat() != B_OK)
		return B_ERROR;
	status_t status = _ResetDeframer();
	if (status != B_OK)
		return status;
	status = _SelectBestAlternate();
	if (status == B_OK)
		status = CamDevice::StartTransfer();
	if (status != B_OK)
		_SelectIdleAlternate();
	return status;
}


status_t
UVCCamDevice::StopTransfer()
{
	// The transfers in flight must finish while the camera still sends.
	// Switching to the idle setting first leaves them waiting for data that
	// never comes, and the data pumps cannot be joined.
	status_t status = CamDevice::StopTransfer();
	_SelectIdleAlternate();
	return status;
}


status_t
UVCCamDevice::SuggestVideoFrame(uint32& width, uint32& height)
{
	printf("UVCCamDevice::SuggestVideoFrame(%" B_PRIu32 ", %" B_PRIu32 ")\n", width, height);
	// 640x480 is a size every UVC camera offers and 30 frames per second fit
	// the bandwidth of a high speed endpoint. If the camera does not have it,
	// take the smallest size it offers.
	width = 640;
	height = 480;
	if (AcceptVideoFrame(width, height) != B_OK) {
		if (fFrames.count == 0)
			return B_ERROR;
		width = fFrames.frames[0].width;
		height = fFrames.frames[0].height;
	}
	return B_OK;
}


status_t
UVCCamDevice::AcceptVideoFrame(uint32& width, uint32& height)
{
	printf("UVCCamDevice::AcceptVideoFrame(%" B_PRIu32 ", %" B_PRIu32 ")\n", width, height);
	if (width <= 0 || height <= 0) {
		// Uncomment below when applications support dimensions other than 320x240
		// This code selects the first listed available uncompressed frame format
		/*
		const usbvc_frame_descriptor* descriptor
			= (const usbvc_frame_descriptor*)fUncompressedFrames.FirstItem();
		width = (*descriptor).width;
		height = (*descriptor).height;
		SetVideoFrame(BRect(0, 0, width - 1, height - 1));
		return B_OK;
		*/

		width  = 640;
		height = 480;
	}

	const uvc_frame* frame = uvc_frame_list_find(&fFrames, width, height);
	if (frame != NULL) {
		fFrame = *frame;
		SetVideoFrame(BRect(0, 0, width - 1, height - 1));
		return B_OK;
	}

	fprintf(stderr, "UVCCamDevice::AcceptVideoFrame() Invalid frame dimensions"
		"\n");
	return B_ERROR;
}


int32
UVCCamDevice::CountVideoSizes() const
{
	return fFrames.count;
}


bool
UVCCamDevice::VideoSizeAt(int32 index, cam_video_size& size) const
{
	if (index < 0 || index >= fFrames.count)
		return false;

	const uvc_frame& frame = fFrames.frames[index];
	size.width = frame.width;
	size.height = frame.height;
	size.interval = frame.interval;
	size.format = frame.compressed ? "MJPEG" : "uncompressed";
	return true;
}


void
UVCCamDevice::DataLost()
{
	static_cast<UVCDeframer*>(fDeframer)->MarkFrameBroken();
}


/*!	Sizes the deframer for the format just committed and drops what is
	left of the last stream.
*/
status_t
UVCCamDevice::_ResetDeframer()
{
	// fDeframer is the UVCDeframer made in the constructor.
	UVCDeframer* deframer = static_cast<UVCDeframer*>(fDeframer);
	return deframer->Reset(uvc_frame_buffer_size(fMaxVideoFrameSize,
		VideoFrame().IntegerWidth() + 1, VideoFrame().IntegerHeight() + 1));
}


status_t
UVCCamDevice::_ProbeCommitFormat()
{
	printf("UVCCamDevice::_ProbeCommitFormat()\n");
	printf("UVCCamDevice::fStreamingIndex = %" B_PRIu32 "\n", fStreamingIndex);

	/*
	char error;
	printf("BEFORE ERROR CODE CHECK.\n");
	fDevice->ControlTransfer(
			USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN, GET_CUR,
			VS_STREAM_ERROR_CODE_CONTROL << 8, fStreamingIndex, 1, &error);
	printf("Error code = Ox%x\n", error);
	*/

	usb_video_probe_and_commit_controls request;
	memset(&request, 0, sizeof(request));
	request._hint.frame_interval = 1;
	request.frame_interval = _DefaultFrameInterval();
	request.format_index = fFrame.formatIndex;
	request.frame_index = fFrame.frameIndex;
	size_t length = fHeaderDescriptor->version > 0x100 ? 34 : 26;
	size_t actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR,
		USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &request);
	if (actualLength != length) {
		fprintf(stderr, "UVCCamDevice::_ProbeFormat() SET_CUR ProbeControl1"
			" failed %ld\n", actualLength);
		return B_ERROR;
	}

	/*
	usbvc_probecommit response;
	actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN, GET_MAX,
		VS_PROBE_CONTROL << 8, fStreamingIndex, sizeof(response), &response);
	if (actualLength != sizeof(response)) {
		fprintf(stderr, "UVCCamDevice::_ProbeFormat() GetMax ProbeControl"
			" failed\n");
		return B_ERROR;
	}

	printf("usbvc_probecommit response.compQuality %d\n", response.compQuality);
	request.compQuality = response.compQuality;
	*/


	usb_video_probe_and_commit_controls response;
	memset(&response, 0, sizeof(response));
	actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN, USB_VIDEO_RC_GET_CUR,
		USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &response);
	if (actualLength != length) {
		fprintf(stderr, "UVCCamDevice::_ProbeFormat() GET_CUR ProbeControl"
			" failed %ld\n", actualLength);
		return B_ERROR;
	}

	/*
	actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, SET_CUR,
		VS_PROBE_CONTROL << 8, fStreamingIndex, length, &request);
	if (actualLength != length) {
		fprintf(stderr, "UVCCamDevice::_ProbeFormat() SetCur ProbeControl2"
			" failed\n");
		return B_ERROR;
	}
	*/

	actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR,
		USB_VIDEO_VS_COMMIT_CONTROL << 8, fStreamingIndex, length, &response);
	if (actualLength != length) {
		fprintf(stderr, "UVCCamDevice::_ProbeFormat() SetCur CommitControl"
			" failed\n");
		return B_ERROR;
	}


	fMaxVideoFrameSize = response.max_video_frame_size;
	fMaxPayloadTransferSize = response.max_payload_transfer_size;
	printf("usbvc_probecommit setup done maxVideoFrameSize:%" B_PRIu32 ""
		" maxPayloadTransferSize:%" B_PRIu32 "\n", fMaxVideoFrameSize,
		fMaxPayloadTransferSize);

	printf("UVCCamDevice::_ProbeCommitFormat()\n --> SUCCESSFUL\n");
	return B_OK;
}


status_t
UVCCamDevice::_SelectBestAlternate()
{
	printf("UVCCamDevice::_SelectBestAlternate()\n");
	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	const BUSBInterface* streaming = config->InterfaceAt(fStreamingIndex);
	if (streaming == NULL)
		return B_BAD_INDEX;

	uint32 bestBandwidth = 0;
	uint32 alternateIndex = 0;
	uint32 endpointIndex = 0;

	for (uint32 i = 0; i < streaming->CountAlternates(); i++) {
		const BUSBInterface* alternate = streaming->AlternateAt(i);
		for (uint32 j = 0; j < alternate->CountEndpoints(); j++) {
			const BUSBEndpoint* endpoint = alternate->EndpointAt(j);
			if (!endpoint->IsIsochronous() || !endpoint->IsInput())
				continue;
			// Bits 0 to 10 are the packet size, bits 11 and 12 the number of
			// additional transactions per interval.
			uint32 packetSize = endpoint->MaxPacketSize();
			uint32 bytesPerInterval = (packetSize & 0x7ff)
				* (1 + ((packetSize >> 11) & 3));
			if (fMaxPayloadTransferSize > bytesPerInterval)
				continue;
			if (bestBandwidth != 0 && bestBandwidth < bytesPerInterval)
				continue;
			bestBandwidth = bytesPerInterval;
			endpointIndex = j;
			alternateIndex = i;
		}
	}

	if (bestBandwidth == 0) {
		fprintf(stderr, "UVCCamDevice::_SelectBestAlternate()"
			" couldn't find a valid alternate\n");
		return B_ERROR;
	}

	printf("UVCCamDevice::_SelectBestAlternate() %" B_PRIu32 "\n", bestBandwidth);
	if (((BUSBInterface*)streaming)->SetAlternate(alternateIndex) != B_OK) {
		fprintf(stderr, "UVCCamDevice::_SelectBestAlternate()"
			" selecting alternate failed\n");
		return B_ERROR;
	}

	fIsoIn = streaming->EndpointAt(endpointIndex);

	return B_OK;
}


/*!	The frame interval the camera proposes for the chosen size, in 100 ns
	units. Not every size runs at 30 frames per second.
*/
uint32
UVCCamDevice::_DefaultFrameInterval() const
{
	const uint32 kThirtyFramesPerSecond = 333333;
	return fFrame.interval != 0 ? fFrame.interval : kThirtyFramesPerSecond;
}



status_t
UVCCamDevice::_SelectIdleAlternate()
{
	printf("UVCCamDevice::_SelectIdleAlternate()\n");
	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	const BUSBInterface* streaming = config->InterfaceAt(fStreamingIndex);
	if (streaming == NULL)
		return B_BAD_INDEX;
	if (((BUSBInterface*)streaming)->SetAlternate(0) != B_OK) {
		fprintf(stderr, "UVCCamDevice::_SelectIdleAlternate()"
			" selecting alternate failed\n");
		return B_ERROR;
	}

	fIsoIn = NULL;

	return B_OK;
}


struct ParameterLayout {
	const char*	group;
	const char*	name;
};


// One entry per parameter, in the order of the parameter numbers.
static const ParameterLayout kLayout[kUvcParameterCount] = {
	{ "Brightness", "Brightness" },
	{ "Contrast", "Contrast" },
	{ "Hue", "Hue" },
	{ "Hue", "Auto" },
	{ "Saturation", "Saturation" },
	{ "Sharpness", "Sharpness" },
	{ "Gamma", "Gamma" },
	{ "White Balance Temperature", "WB Temperature" },
	{ "White Balance Temperature", "Auto" },
	{ "White Balance Component", "WB Blue" },
	{ "White Balance Component", "Auto" },
	{ "Backlight Compensation", "Backlight Compensation" },
	{ "Gain", "Gain" },
	{ "Power Line Frequency", "Frequency" },
	{ "White Balance Component", "WB Red" },
	{ "Exposure", "Auto" },
	{ "Exposure", "Exposure Time" },
	{ "Exposure", "Allow Lower Frame Rate" }
};


// The order in which the parameters appear in a group: the automatic switch
// after the values it governs.
static const int32 kCreationOrder[kUvcParameterCount] = {
	kUvcParameterBrightness, kUvcParameterContrast, kUvcParameterHue, kUvcParameterHueAuto,
	kUvcParameterSaturation, kUvcParameterSharpness, kUvcParameterGamma,
	kUvcParameterWhiteBalanceTemperature, kUvcParameterWhiteBalanceTemperatureAuto,
	kUvcParameterWhiteBalanceBlue, kUvcParameterWhiteBalanceRed,
	kUvcParameterWhiteBalanceComponentAuto, kUvcParameterBacklightCompensation,
	kUvcParameterGain, kUvcParameterPowerLineFrequency, kUvcParameterExposureAuto,
	kUvcParameterExposureTime, kUvcParameterExposureFrameRate
};


void
UVCCamDevice::_DefineControl(int32 parameter, UVCControlKind kind, uint8 selector,
	uint16 requestIndex, uint8 length, int32 autoParameter)
{
	UVCControl& control = fControls[parameter];
	control.kind = kind;
	control.selector = selector;
	control.requestIndex = requestIndex;
	control.length = length;
	control.autoParameter = autoParameter;
	control.minimum = 0;
	control.maximum = 100;
	control.value = 0;
}


/*!	Decides from the descriptors which controls the camera has. The bit
	numbers are those of the UVC specification: Processing Unit byte 0 and 1,
	Camera Terminal byte 0.
*/
void
UVCCamDevice::_DefineControls()
{
	for (int32 i = 0; i < kUvcParameterCount; i++)
		fControls[i].kind = kUvcControlNone;

	const uint16 unit = fControlRequestIndex;
	const uint8 first = fProcessingControls[0];
	const uint8 second = fProcessingControls[1];

	if ((first & 0x01) != 0) {
		_DefineControl(kUvcParameterBrightness, kUvcControlRange,
			USB_VIDEO_PU_BRIGHTNESS_CONTROL, unit, 2, -1);
	}
	if ((first & 0x02) != 0) {
		_DefineControl(kUvcParameterContrast, kUvcControlRange,
			USB_VIDEO_PU_CONTRAST_CONTROL, unit, 2, -1);
	}
	if ((first & 0x04) != 0) {
		_DefineControl(kUvcParameterHue, kUvcControlRange, USB_VIDEO_PU_HUE_CONTROL,
			unit, 2, (second & 0x08) != 0 ? kUvcParameterHueAuto : -1);
	}
	if ((second & 0x08) != 0) {
		_DefineControl(kUvcParameterHueAuto, kUvcControlSwitch,
			USB_VIDEO_PU_HUE_AUTO_CONTROL, unit, 1, -1);
	}
	if ((first & 0x08) != 0) {
		_DefineControl(kUvcParameterSaturation, kUvcControlRange,
			USB_VIDEO_PU_SATURATION_CONTROL, unit, 2, -1);
	}
	if ((first & 0x10) != 0) {
		_DefineControl(kUvcParameterSharpness, kUvcControlRange,
			USB_VIDEO_PU_SHARPNESS_CONTROL, unit, 2, -1);
	}
	if ((first & 0x20) != 0) {
		_DefineControl(kUvcParameterGamma, kUvcControlRange,
			USB_VIDEO_PU_GAMMA_CONTROL, unit, 2, -1);
	}
	if ((first & 0x40) != 0) {
		_DefineControl(kUvcParameterWhiteBalanceTemperature, kUvcControlRange,
			USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL, unit, 2,
			(second & 0x10) != 0 ? kUvcParameterWhiteBalanceTemperatureAuto : -1);
	}
	if ((second & 0x10) != 0) {
		_DefineControl(kUvcParameterWhiteBalanceTemperatureAuto, kUvcControlSwitch,
			USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL, unit, 1, -1);
	}
	if ((first & 0x80) != 0) {
		// Blue and red share one control of four bytes.
		const int32 automatic
			= (second & 0x20) != 0 ? kUvcParameterWhiteBalanceComponentAuto : -1;
		_DefineControl(kUvcParameterWhiteBalanceBlue, kUvcControlRange,
			USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_CONTROL, unit, 4, automatic);
		_DefineControl(kUvcParameterWhiteBalanceRed, kUvcControlRange,
			USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_CONTROL, unit, 4, automatic);
	}
	if ((second & 0x20) != 0) {
		_DefineControl(kUvcParameterWhiteBalanceComponentAuto, kUvcControlSwitch,
			USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL, unit, 1, -1);
	}
	if ((second & 0x01) != 0) {
		_DefineControl(kUvcParameterBacklightCompensation, kUvcControlRange,
			USB_VIDEO_PU_BACKLIGHT_COMPENSATION_CONTROL, unit, 2, -1);
	}
	if ((second & 0x02) != 0) {
		_DefineControl(kUvcParameterGain, kUvcControlRange, USB_VIDEO_PU_GAIN_CONTROL,
			unit, 2, -1);
	}
	if ((second & 0x04) != 0) {
		_DefineControl(kUvcParameterPowerLineFrequency, kUvcControlChoice,
			USB_VIDEO_PU_POWER_LINE_FREQUENCY_CONTROL, unit, 1, -1);
	}

	const uint16 camera = fCameraRequestIndex;
	const uint8 terminal = fCameraControls;
	if ((terminal & 0x02) != 0) {
		_DefineControl(kUvcParameterExposureAuto, kUvcControlExposureMode,
			USB_VIDEO_CT_AE_MODE_CONTROL, camera, 1, -1);
	}
	if ((terminal & 0x08) != 0) {
		_DefineControl(kUvcParameterExposureTime, kUvcControlRange,
			USB_VIDEO_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL, camera, 4,
			(terminal & 0x02) != 0 ? kUvcParameterExposureAuto : -1);
	}
	if ((terminal & 0x04) != 0) {
		_DefineControl(kUvcParameterExposureFrameRate, kUvcControlSwitch,
			USB_VIDEO_CT_AE_PRIORITY_CONTROL, camera, 1, -1);
	}
}


bool
UVCCamDevice::_ReadControl(const UVCControl& control, uint8 request, uint8* data)
{
	ssize_t transferred = fDevice->ControlTransfer(USB_REQTYPE_CLASS
		| USB_REQTYPE_INTERFACE_IN, request, control.selector << 8,
		control.requestIndex, control.length, data);
	return uvc_transfer_succeeded(transferred, control.length);
}


/*!	Asks the camera for the range and the current value of a control. A
	control that does not answer keeps the defaults.
*/
void
UVCCamDevice::_LoadControl(int32 parameter)
{
	UVCControl& control = fControls[parameter];
	uint8 data[4];
	const bool isSigned = control.selector != USB_VIDEO_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL
		|| control.requestIndex != fCameraRequestIndex;

	if (control.kind == kUvcControlRange) {
		if (control.length == 4 && control.requestIndex == fControlRequestIndex) {
			_LoadWhiteBalanceComponent();
			return;
		}
		if (_ReadControl(control, USB_VIDEO_RC_GET_MIN, data))
			control.minimum = uvc_decode_value(data, control.length, isSigned);
		if (_ReadControl(control, USB_VIDEO_RC_GET_MAX, data))
			control.maximum = uvc_decode_value(data, control.length, isSigned);
		if (_ReadControl(control, USB_VIDEO_RC_GET_CUR, data))
			control.value = uvc_decode_value(data, control.length, isSigned);

		// A range of two values is a switch.
		if (control.maximum - control.minimum == 1) {
			control.kind = kUvcControlSwitch;
			control.value = control.value != control.minimum ? 1 : 0;
		}
		return;
	}

	if (control.kind == kUvcControlExposureMode) {
		uint8 supported = 0;
		if (_ReadControl(control, USB_VIDEO_RC_GET_RES, data))
			supported = data[0];
		control.maximum = supported;
	}
	if (_ReadControl(control, USB_VIDEO_RC_GET_CUR, data)) {
		const int32 current = uvc_decode_value(data, control.length, false);
		if (control.kind == kUvcControlExposureMode)
			control.value = uvc_exposure_mode_is_automatic((uint8)current) ? 1 : 0;
		else if (control.kind == kUvcControlSwitch)
			control.value = current != 0 ? 1 : 0;
		else
			control.value = current;
	}
}


/*!	Blue and red come in one control of two 16 bit values, blue first. */
void
UVCCamDevice::_LoadWhiteBalanceComponent()
{
	UVCControl& blue = fControls[kUvcParameterWhiteBalanceBlue];
	UVCControl& red = fControls[kUvcParameterWhiteBalanceRed];
	uint8 data[4];
	int16 first, second;

	if (_ReadControl(blue, USB_VIDEO_RC_GET_MIN, data)) {
		uvc_unpack_pair(data, &first, &second);
		blue.minimum = first;
		red.minimum = second;
	}
	if (_ReadControl(blue, USB_VIDEO_RC_GET_MAX, data)) {
		uvc_unpack_pair(data, &first, &second);
		blue.maximum = first;
		red.maximum = second;
	}
	if (_ReadControl(blue, USB_VIDEO_RC_GET_CUR, data)) {
		uvc_unpack_pair(data, &first, &second);
		blue.value = first;
		red.value = second;
	}
}


/*!	Sends the stored value of a control to the camera. */
status_t
UVCCamDevice::_WriteControl(int32 parameter)
{
	const UVCControl& control = fControls[parameter];
	uint8 data[4];
	uint8 length = control.length;

	if (control.kind == kUvcControlExposureMode) {
		const uint8 mode = uvc_exposure_mode(control.value != 0,
			(uint8)control.maximum);
		if (mode == 0)
			return B_NOT_SUPPORTED;
		uvc_encode_value(mode, 1, data);
	} else if (control.kind == kUvcControlRange && length == 4
		&& control.requestIndex == fControlRequestIndex) {
		uvc_pack_pair((int16)fControls[kUvcParameterWhiteBalanceBlue].value,
			(int16)fControls[kUvcParameterWhiteBalanceRed].value, data);
	} else
		uvc_encode_value(control.value, length, data);

	ssize_t transferred = fDevice->ControlTransfer(USB_REQTYPE_CLASS
		| USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR, control.selector << 8,
		control.requestIndex, length, data);
	return uvc_transfer_succeeded(transferred, length) ? B_OK : B_ERROR;
}


/*!	While the camera adjusts a value on its own, it shows the current value. */
void
UVCCamDevice::_RefreshAutomaticValue(int32 parameter)
{
	UVCControl& control = fControls[parameter];
	if (control.kind != kUvcControlRange || control.autoParameter < 0
		|| fControls[control.autoParameter].value == 0)
		return;

	uint8 data[4];
	if (!_ReadControl(control, USB_VIDEO_RC_GET_CUR, data))
		return;

	if (control.length == 4 && control.requestIndex == fControlRequestIndex) {
		int16 blue, red;
		uvc_unpack_pair(data, &blue, &red);
		fControls[kUvcParameterWhiteBalanceBlue].value = blue;
		fControls[kUvcParameterWhiteBalanceRed].value = red;
	} else {
		const bool isSigned = control.requestIndex != fCameraRequestIndex;
		control.value = uvc_decode_value(data, control.length, isSigned);
	}
}


void
UVCCamDevice::AddParameters(BParameterGroup* group, int32& index)
{
	fFirstParameterID = index;
	CamDevice::AddParameters(group, index);

	_DefineControls();
	for (int32 i = 0; i < kUvcParameterCount; i++) {
		if (fControls[i].kind != kUvcControlNone)
			_LoadControl(i);
	}

	BParameterGroup* groups[kUvcParameterCount];
	for (int32 i = 0; i < kUvcParameterCount; i++)
		groups[i] = NULL;

	for (int32 i = 0; i < kUvcParameterCount; i++) {
		const int32 parameter = kCreationOrder[i];
		if (fControls[parameter].kind == kUvcControlNone)
			continue;

		groups[parameter] = _GroupFor(group, groups, parameter);
		_MakeParameter(groups[parameter], parameter);
	}

	index += kUvcParameterCount;
}


/*!	All parameters of one feature share a group, named like the feature. */
BParameterGroup*
UVCCamDevice::_GroupFor(BParameterGroup* group, BParameterGroup** groups,
	int32 parameter)
{
	for (int32 i = 0; i < kUvcParameterCount; i++) {
		if (groups[i] != NULL
			&& strcmp(kLayout[i].group, kLayout[parameter].group) == 0)
			return groups[i];
	}
	return group->MakeGroup(kLayout[parameter].group);
}


void
UVCCamDevice::_MakeParameter(BParameterGroup* group, int32 parameter)
{
	const UVCControl& control = fControls[parameter];
	const int32 id = fFirstParameterID + parameter;
	const char* name = kLayout[parameter].name;

	switch (control.kind) {
		case kUvcControlRange:
			group->MakeContinuousParameter(id, B_MEDIA_RAW_VIDEO, name, B_GAIN,
				"", control.minimum, control.maximum, 1.0);
			break;

		case kUvcControlChoice:
		{
			BDiscreteParameter* choice = group->MakeDiscreteParameter(id,
				B_MEDIA_RAW_VIDEO, name, B_INPUT_MUX);
			choice->AddItem(0, "Off");
			choice->AddItem(1, "50 Hz");
			choice->AddItem(2, "60 Hz");
			break;
		}

		case kUvcControlSwitch:
		case kUvcControlExposureMode:
			group->MakeDiscreteParameter(id, B_MEDIA_RAW_VIDEO, name, B_ENABLE);
			break;

		default:
			break;
	}
}


status_t
UVCCamDevice::GetParameterValue(int32 id, bigtime_t* last_change, void* value,
	size_t* size)
{
	const int32 parameter = id - fFirstParameterID;
	if (parameter < 0 || parameter >= kUvcParameterCount)
		return B_BAD_VALUE;

	const UVCControl& control = fControls[parameter];
	if (control.kind == kUvcControlNone)
		return B_BAD_VALUE;

	const bool isNumber = control.kind == kUvcControlRange;
	const size_t valueSize = isNumber ? sizeof(float) : sizeof(int32);
	if (*size < valueSize)
		return B_BAD_VALUE;

	_RefreshAutomaticValue(parameter);
	if (isNumber)
		*(float*)value = (float)control.value;
	else
		*(int32*)value = control.value;
	*size = valueSize;
	*last_change = fLastParameterChanges;
	return B_OK;
}


status_t
UVCCamDevice::SetParameterValue(int32 id, bigtime_t when, const void* value,
	size_t size)
{
	const int32 parameter = id - fFirstParameterID;
	if (parameter < 0 || parameter >= kUvcParameterCount)
		return B_BAD_VALUE;

	UVCControl& control = fControls[parameter];
	if (control.kind == kUvcControlNone || value == NULL)
		return B_BAD_VALUE;

	if (control.kind == kUvcControlRange) {
		if (size != sizeof(float))
			return B_BAD_VALUE;
		control.value = uvc_clamp_control_value(*(const float*)value,
			control.minimum, control.maximum);
	} else {
		if (size != sizeof(int32))
			return B_BAD_VALUE;
		const int32 requested = *(const int32*)value;
		control.value = control.kind == kUvcControlChoice
			? uvc_clamp_control_value((float)requested, 0, 2)
			: (requested != 0 ? 1 : 0);
	}
	fLastParameterChanges = when;

	// The camera ignores a value that it adjusts on its own.
	if (control.autoParameter >= 0 && fControls[control.autoParameter].value != 0)
		return B_OK;

	status_t status = _WriteControl(parameter);
	if (status == B_OK && _IsAutomaticSwitch(parameter) && control.value == 0)
		status = _WriteDependentValues(parameter);
	return status;
}


bool
UVCCamDevice::_IsAutomaticSwitch(int32 parameter) const
{
	for (int32 i = 0; i < kUvcParameterCount; i++) {
		if (fControls[i].kind != kUvcControlNone && fControls[i].autoParameter == parameter)
			return true;
	}
	return false;
}


/*!	When the camera stops adjusting a value, it takes the one the user last
	chose. Returns the first error, but writes the other values anyway.
*/
status_t
UVCCamDevice::_WriteDependentValues(int32 automaticParameter)
{
	status_t result = B_OK;
	bool pairWritten = false;
	for (int32 i = 0; i < kUvcParameterCount; i++) {
		const UVCControl& control = fControls[i];
		if (control.kind == kUvcControlNone || control.autoParameter != automaticParameter)
			continue;

		// Blue and red go out together, once.
		const bool isPair = i == kUvcParameterWhiteBalanceBlue
			|| i == kUvcParameterWhiteBalanceRed;
		if (isPair && pairWritten)
			continue;
		pairWritten = pairWritten || isPair;
		status_t status = _WriteControl(i);
		if (status != B_OK && result == B_OK)
			result = status;
	}
	return result;
}


status_t
UVCCamDevice::FillFrameBuffer(BBuffer* buffer, bigtime_t* stamp)
{
	status_t err = fDeframer->WaitFrame(2000000);
	if (err < B_OK) {
		fprintf(stderr, "WaitFrame: %" B_PRIx32 "\n", err);
		return err;
	}

	CamFrame* f;
	err = fDeframer->GetFrame(&f, stamp);
	if (err < B_OK) {
		fprintf(stderr, "GetFrame: %" B_PRIx32 "\n", err);
		return err;
	}

	uvc_picture picture;
	picture.bits = (uint8*)buffer->Data();
	picture.size = buffer->SizeAvailable();
	picture.width = VideoFrame().IntegerWidth() + 1;
	picture.height = VideoFrame().IntegerHeight() + 1;
	err = _Decode(*f, picture);
	delete f;
	return err;
}


/*!	Converts a frame into the B_RGB32 picture. A damaged frame, one that is
	shorter than its format promises or that does not decode, is not shown at
	all; an empty buffer would flash black.
*/
status_t
UVCCamDevice::_Decode(const CamFrame& frame, const uvc_picture& picture)
{
	const uint8* data = (const uint8*)frame.Buffer();
	if (fFrame.compressed) {
		return fJpegDecoder.Decode(data, frame.BufferLength(), picture)
			? B_OK : B_BAD_DATA;
	}

	if (!uvc_yuy2_frame_fits(frame.BufferLength(), picture.size,
			picture.width, picture.height)) {
		return B_BAD_DATA;
	}
	// TODO: The Video Producer only outputs B_RGB32.  This is OK for most
	// applications.  This could be leveraged if applications can
	// consume B_YUV422.
	uvc_decode_yuy2(picture.bits, data, picture.width, picture.height);
	return B_OK;
}


void
UVCCamDevice::_AddFrame(const usb_video_frame_descriptor& descriptor,
	bool compressed)
{
	uvc_frame frame;
	frame.width = descriptor.width;
	frame.height = descriptor.height;
	frame.formatIndex = fParsedFormatIndex;
	// The probe request wants the descriptor's own frame index, which
	// starts at 1, not the position in our list.
	frame.frameIndex = descriptor.frame_index;
	frame.interval = descriptor.default_frame_interval;
	frame.compressed = compressed;
	uvc_frame_list_add(&fFrames, frame);
}



UVCCamDeviceAddon::UVCCamDeviceAddon(WebCamMediaAddOn* webcam)
	: CamDeviceAddon(webcam)
{
	printf("UVCCamDeviceAddon::UVCCamDeviceAddon(WebCamMediaAddOn* webcam)\n");
	SetSupportedDevices(kSupportedDevices);
}


UVCCamDeviceAddon::~UVCCamDeviceAddon()
{
}


const char *
UVCCamDeviceAddon::BrandName()
{
	printf("UVCCamDeviceAddon::BrandName()\n");
	return "USB Video Class";
}


UVCCamDevice *
UVCCamDeviceAddon::Instantiate(CamRoster& roster, BUSBDevice* from)
{
	printf("UVCCamDeviceAddon::Instantiate()\n");
	return new UVCCamDevice(*this, from);
}


extern "C" status_t
B_WEBCAM_MKINTFUNC(uvccam)
(WebCamMediaAddOn* webcam, CamDeviceAddon **addon)
{
	*addon = new UVCCamDeviceAddon(webcam);
	return B_OK;
}
