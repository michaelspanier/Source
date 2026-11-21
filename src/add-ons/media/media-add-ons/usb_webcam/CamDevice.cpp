/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */


#include "CamDevice.h"
#include "CamSensor.h"
#include "CamDeframer.h"
#include "CamDebug.h"
#include "CamDeviceName.h"
#include "AddOn.h"

#include <OS.h>
#include <Autolock.h>

//#define DEBUG_WRITE_DUMP
//#define DEBUG_DISCARD_DATA
//#define DEBUG_READ_DUMP
//#define DEBUG_DISCARD_INPUT

#undef B_WEBCAM_DECLARE_SENSOR
#define B_WEBCAM_DECLARE_SENSOR(sensorclass,sensorname) \
extern "C" CamSensor *Instantiate##sensorclass(CamDevice *cam);
#include "CamInternalSensors.h"
#undef B_WEBCAM_DECLARE_SENSOR
typedef CamSensor *(*SensorInstFunc)(CamDevice *cam);
struct { const char *name; SensorInstFunc instfunc; } kSensorTable[] = {
#define B_WEBCAM_DECLARE_SENSOR(sensorclass,sensorname) \
{ #sensorname, &Instantiate##sensorclass },
#include "CamInternalSensors.h"
{ NULL, NULL },
};
#undef B_WEBCAM_DECLARE_SENSOR


CamDevice::CamDevice(CamDeviceAddon &_addon, BUSBDevice* _device)
	: fInitStatus(B_NO_INIT),
	  fSensor(NULL),
	  fBulkIn(NULL),
	  fIsoIn(NULL),
	  fLastParameterChanges(0),
	  fCamDeviceAddon(_addon),
	  fDevice(_device),
	  fSupportedDeviceIndex(-1),
	  fChipIsBigEndian(false),
	  fTransferEnabled(false),
	  fLocker("WebcamDeviceLock"),
	  fIsoQueued(0),
	  fIsoDelivered(0)
{
	// fill in the generic flavor
	_addon.WebCamAddOn()->FillDefaultFlavorInfo(&fFlavorInfo);
	// if we use id matching, cache the index to the list
	if (fCamDeviceAddon.SupportedDevices()) {
		fSupportedDeviceIndex = fCamDeviceAddon.Sniff(_device);
		fFlavorInfoNameStr = "";
		fFlavorInfoNameStr << fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].vendor << " USB Webcam";
		fFlavorInfoInfoStr = "";
		fFlavorInfoInfoStr << fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].vendor;
		fFlavorInfoInfoStr << " (" << fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].product << ") USB Webcam";
		fFlavorInfo.name = fFlavorInfoNameStr.String();
		fFlavorInfo.info = fFlavorInfoInfoStr.String();
	}

	// All cameras of one add-on get the same generic name above; the product
	// string of the device tells them apart, for example "Integrated Camera".
	char deviceName[B_MEDIA_NAME_LENGTH];
	if (cam_device_name(deviceName, sizeof(deviceName),
			_device->ProductString())) {
		fFlavorInfoNameStr = deviceName;
		fFlavorInfo.name = fFlavorInfoNameStr.String();
	}
#ifdef DEBUG_WRITE_DUMP
	fDumpFD = open("/boot/home/webcam.out", O_CREAT|O_RDWR, 0644);
#endif
#ifdef DEBUG_READ_DUMP
	fDumpFD = open("/boot/home/webcam.out", O_RDONLY, 0644);
#endif
	fBufferLen = 1*B_PAGE_SIZE;
	fBuffer = (uint8 *)malloc(fBufferLen);
}


CamDevice::~CamDevice()
{
	close(fDumpFD);
	free(fBuffer);
	if (fDeframer)
		delete fDeframer;
}


status_t
CamDevice::InitCheck()
{
	return fInitStatus;
}


bool
CamDevice::Matches(BUSBDevice* _device)
{
	return _device == fDevice;
}


BUSBDevice*
CamDevice::GetDevice()
{
	return fDevice;
}


void
CamDevice::Unplugged()
{
	fDevice = NULL;
	fBulkIn = NULL;
	fIsoIn = NULL;
}


bool
CamDevice::IsPlugged()
{
	return (fDevice != NULL);
}


const char *
CamDevice::BrandName()
{
	if (fCamDeviceAddon.SupportedDevices() && (fSupportedDeviceIndex > -1))
		return fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].vendor;
	return "<unknown>";
}


const char *
CamDevice::ModelName()
{
	if (fCamDeviceAddon.SupportedDevices() && (fSupportedDeviceIndex > -1))
		return fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].product;
	return "<unknown>";
}


bool
CamDevice::SupportsBulk()
{
	return false;
}


bool
CamDevice::SupportsIsochronous()
{
	return false;
}


status_t
CamDevice::StartTransfer()
{
	status_t err = B_OK;
	PRINT((CH "()" CT));
	if (fTransferEnabled)
		return EALREADY;
	fPumpThread = spawn_thread(_DataPumpThread, "USB Webcam Data Pump", 50,
		this);
	if (fPumpThread < B_OK)
		return fPumpThread;
	if (fSensor)
		err = fSensor->StartTransfer();
	if (err < B_OK)
		return err;
	fTransferEnabled = true;
	resume_thread(fPumpThread);
	PRINT((CH ": transfer enabled" CT));
	return B_OK;
}


status_t
CamDevice::StopTransfer()
{
	status_t err = B_OK;
	PRINT((CH "()" CT));
	if (!fTransferEnabled)
		return EALREADY;
	if (fSensor)
		err = fSensor->StopTransfer();
	if (err < B_OK)
		return err;
	fTransferEnabled = false;

	// the thread itself might Lock()
	fLocker.Unlock();
	wait_for_thread(fPumpThread, &err);
	fLocker.Lock();

	return B_OK;
}


status_t
CamDevice::SuggestVideoFrame(uint32 &width, uint32 &height)
{
	if (Sensor()) {
		width = Sensor()->MaxWidth();
		height = Sensor()->MaxHeight();
		return B_OK;
	}
	return B_NO_INIT;
}


status_t
CamDevice::AcceptVideoFrame(uint32 &width, uint32 &height)
{
	status_t err = ENOSYS;
	if (Sensor())
		err = Sensor()->AcceptVideoFrame(width, height);
	if (err < B_OK)
		return err;
	SetVideoFrame(BRect(0, 0, width - 1, height - 1));
	return B_OK;
}


status_t
CamDevice::SetVideoFrame(BRect frame)
{
	fVideoFrame = frame;
	return B_OK;
}


status_t
CamDevice::SetScale(float scale)
{
	return B_OK;
}


status_t
CamDevice::SetVideoParams(float brightness, float contrast, float hue,
	float red, float green, float blue)
{
	return B_OK;
}


void
CamDevice::AddParameters(BParameterGroup *group, int32 &index)
{
	fFirstParameterID = index;
}


status_t
CamDevice::GetParameterValue(int32 id, bigtime_t *last_change, void *value,
	size_t *size)
{
	return B_BAD_VALUE;
}


status_t
CamDevice::SetParameterValue(int32 id, bigtime_t when, const void *value,
	size_t size)
{
	return B_BAD_VALUE;
}


size_t
CamDevice::MinRawFrameSize()
{
	return 0;
}


size_t
CamDevice::MaxRawFrameSize()
{
	return 0;
}


bool
CamDevice::ValidateStartOfFrameTag(const uint8 *tag, size_t taglen)
{
	return true;
}


bool
CamDevice::ValidateEndOfFrameTag(const uint8 *tag, size_t taglen,
	size_t datalen)
{
	return true;
}


status_t
CamDevice::WaitFrame(bigtime_t timeout)
{
	if (fDeframer)
		return WaitFrame(timeout);
	return EINVAL;
}


status_t
CamDevice::GetFrameBitmap(BBitmap **bm, bigtime_t *stamp)
{
	return EINVAL;
}


status_t
CamDevice::FillFrameBuffer(BBuffer *buffer, bigtime_t *stamp)
{
	return EINVAL;
}


bool
CamDevice::Lock()
{
	return fLocker.Lock();
}


status_t
CamDevice::PowerOnSensor(bool on)
{
	return B_OK;
}


ssize_t
CamDevice::WriteReg(uint16 address, uint8 *data, size_t count)
{
	return ENOSYS;
}


ssize_t
CamDevice::WriteReg8(uint16 address, uint8 data)
{
	return WriteReg(address, &data, sizeof(uint8));
}


ssize_t
CamDevice::WriteReg16(uint16 address, uint16 data)
{
	if (fChipIsBigEndian)
		data = B_HOST_TO_BENDIAN_INT16(data);
	else
		data = B_HOST_TO_LENDIAN_INT16(data);
	return WriteReg(address, (uint8 *)&data, sizeof(uint16));
}


ssize_t
CamDevice::ReadReg(uint16 address, uint8 *data, size_t count, bool cached)
{
	return ENOSYS;
}


ssize_t
CamDevice::OrReg8(uint16 address, uint8 data, uint8 mask)
{
	uint8 value;
	if (ReadReg(address, &value, 1, true) < 1)
		return EIO;
	value &= mask;
	value |= data;
	return WriteReg8(address, value);
}


ssize_t
CamDevice::AndReg8(uint16 address, uint8 data)
{
	uint8 value;
	if (ReadReg(address, &value, 1, true) < 1)
		return EIO;
	value &= data;
	return WriteReg8(address, value);
}


/*
status_t
CamDevice::GetStatusIIC()
{
	return ENOSYS;
}
*/

/*status_t
CamDevice::WaitReadyIIC()
{
	return ENOSYS;
}
*/

ssize_t
CamDevice::WriteIIC(uint8 address, uint8 *data, size_t count)
{
	return ENOSYS;
}


ssize_t
CamDevice::WriteIIC8(uint8 address, uint8 data)
{
	return WriteIIC(address, &data, 1);
}


ssize_t
CamDevice::WriteIIC16(uint8 address, uint16 data)
{
	if (Sensor() && Sensor()->IsBigEndian())
		data = B_HOST_TO_BENDIAN_INT16(data);
	else
		data = B_HOST_TO_LENDIAN_INT16(data);
	return WriteIIC(address, (uint8 *)&data, 2);
}


ssize_t
CamDevice::ReadIIC(uint8 address, uint8 *data)
{
	//TODO: make it mode generic
	return ENOSYS;
}


ssize_t
CamDevice::ReadIIC8(uint8 address, uint8 *data)
{
	return ReadIIC(address, data);
}


ssize_t
CamDevice::ReadIIC16(uint8 address, uint16 *data)
{
	return ENOSYS;
}


status_t
CamDevice::SetIICBitsMode(size_t bits)
{
	return ENOSYS;
}


status_t
CamDevice::ProbeSensor()
{
	const usb_webcam_support_descriptor *devs;
	const usb_webcam_support_descriptor *dev = NULL;
	status_t err;
	int32 i;

	PRINT((CH ": probing sensors..." CT));
	if (fCamDeviceAddon.SupportedDevices() == NULL)
		return B_ERROR;
	devs = fCamDeviceAddon.SupportedDevices();
	for (i = 0; devs[i].vendor; i++) {
		if (GetDevice()->VendorID() != devs[i].desc.vendor)
			continue;
		if (GetDevice()->ProductID() != devs[i].desc.product)
			continue;
		dev = &devs[i];
		break;
	}
	if (!dev)
		return ENODEV;
	if (!dev->sensors) // no usable sensor
		return ENOENT;
	BString sensors(dev->sensors);
	for (i = 0; i > -1 && i < sensors.Length(); ) {
		BString name;
		sensors.CopyInto(name, i, sensors.FindFirst(',', i) - i);
		PRINT((CH ": probing sensor '%s'..." CT, name.String()));

		fSensor = CreateSensor(name.String());
		if (fSensor) {
			err = fSensor->Probe();
			if (err >= B_OK)
				return B_OK;

			PRINT((CH ": sensor '%s' Probe: %s" CT, name.String(),
				strerror(err)));

			delete fSensor;
			fSensor = NULL;
		}

		i = sensors.FindFirst(',', i+1);
		if (i > - 1)
			i++;
	}
	return ENOENT;
}


CamSensor *
CamDevice::CreateSensor(const char *name)
{
	for (int32 i = 0; kSensorTable[i].name; i++) {
		if (!strcmp(kSensorTable[i].name, name))
			return kSensorTable[i].instfunc(this);
	}
	PRINT((CH ": sensor '%s' not found" CT, name));
	return NULL;
}


void
CamDevice::SetDataInput(BDataIO *input)
{
	fDataInput = input;
}


status_t
CamDevice::DataPumpThread()
{
	if (SupportsBulk()) {
		PRINT((CH ": using Bulk" CT));
		while (fTransferEnabled) {
			ssize_t len = -1;
			BAutolock lock(fLocker);
			if (!lock.IsLocked())
				break;
			if (!fBulkIn)
				break;
#ifndef DEBUG_DISCARD_INPUT
			len = fBulkIn->BulkTransfer(fBuffer, fBufferLen);
#endif

			//PRINT((CH ": got %ld bytes" CT, len));
#ifdef DEBUG_WRITE_DUMP
			write(fDumpFD, fBuffer, len);
#endif
#ifdef DEBUG_READ_DUMP
			if ((len = read(fDumpFD, fBuffer, fBufferLen)) < fBufferLen)
				lseek(fDumpFD, 0LL, SEEK_SET);
#endif

			if (len <= 0) {
				PRINT((CH ": BulkIn: %s" CT, strerror(len)));
				break;
			}

#ifndef DEBUG_DISCARD_DATA
			if (fDataInput) {
				fDataInput->Write(fBuffer, len);
				// else drop
			}
#endif
			//snooze(2000);
		}
	}
#ifdef SUPPORT_ISO
	else if (SupportsIsochronous()) {
		// Two transfers are kept in flight. A single synchronous transfer
		// leaves the endpoint without a request between one transfer and the
		// next, and the camera keeps sending, so packets would get lost.
		thread_id second = spawn_thread(_IsoPumpThread, "USB Webcam Data Pump 2", 50, this);
		if (second >= B_OK)
			resume_thread(second);
		_IsoPump();
		if (second >= B_OK) {
			status_t result;
			wait_for_thread(second, &result);
		}
	}
#endif
	else {
		PRINT((CH ": No supported transport." CT));
		return B_UNSUPPORTED;
	}
	return B_OK;
}


static const int kMaxIsoPackets = 128;
static const size_t kMaxIsoTransferBytes = 64 * 1024;
static const int kIsoTurnTries = 50;
static const bigtime_t kIsoTurnWait = 100;


struct iso_transfer {
	uint8*						buffer;
	size_t						packetLength;
	int							packetCount;
	bool						received;
	usb_iso_packet_descriptor	packets[kMaxIsoPackets];
};


/*!	The room a packet of \a endpoint needs per interval: bits 0 to 10 of the
	maximum packet size are the size, bits 11 and 12 the extra transactions.
*/
static size_t
iso_packet_length(const BUSBEndpoint* endpoint)
{
	uint16 maxPacketSize = endpoint->MaxPacketSize();
	size_t length = (maxPacketSize & 0x7ff) * (1 + ((maxPacketSize >> 11) & 3));
	return length != 0 ? length : 256;
}


//!	Does nothing; a camera whose deframer can drop a frame overrides it.
void
CamDevice::DataLost()
{
}


status_t
CamDevice::_IsoPump()
{
	const BUSBEndpoint* endpoint = fIsoIn;
	if (endpoint == NULL)
		return B_NO_INIT;

	// The USB stack needs one contiguous buffer per transfer, so a transfer
	// with large packets gets fewer of them.
	iso_transfer transfer;
	transfer.packetLength = iso_packet_length(endpoint);
	transfer.packetCount = kMaxIsoTransferBytes / transfer.packetLength;
	if (transfer.packetCount > kMaxIsoPackets)
		transfer.packetCount = kMaxIsoPackets;
	if (transfer.packetCount < 1)
		transfer.packetCount = 1;

	const size_t bufferLength = transfer.packetCount * transfer.packetLength;
	transfer.buffer = (uint8*)malloc(bufferLength);
	if (transfer.buffer == NULL)
		return B_NO_MEMORY;

	for (int i = 0; i < transfer.packetCount; i++)
		transfer.packets[i].request_length = transfer.packetLength;

	while (fTransferEnabled) {
		const int64 ticket = _TakeIsoTicket();
		ssize_t length = endpoint->IsochronousTransfer(transfer.buffer,
			bufferLength, transfer.packets, transfer.packetCount);
		transfer.received = length > 0;
		_DeliverIsoTransfer(ticket, transfer);
		if (!transfer.received) {
			PRINT((CH ": IsoIn: %s" CT, strerror(length)));
			snooze(1000);
		}
	}

	free(transfer.buffer);
	return B_OK;
}


//!	Numbers the transfers in the order they are queued.
int64
CamDevice::_TakeIsoTicket()
{
	BAutolock order(fIsoOrder);
	return fIsoQueued++;
}


/*!	Two transfers are in flight, and the thread that wakes first does not
	always get here first. The transfers are passed on in the order of their
	tickets. One that comes too late, or holds a damaged packet, costs the
	frame it falls into.
*/
void
CamDevice::_DeliverIsoTransfer(int64 ticket, const iso_transfer& transfer)
{
	BAutolock order(fIsoOrder);
	const bool inTurn = _WaitIsoTurn(ticket);
	if (fIsoDelivered <= ticket)
		fIsoDelivered = ticket + 1;

	if (fDataInput == NULL)
		return;
	if (!inTurn || !transfer.received) {
		DataLost();
		return;
	}

	// Each packet sits at its own slot of packetLength bytes.
	for (int i = 0; i < transfer.packetCount; i++) {
		const usb_iso_packet_descriptor& packet = transfer.packets[i];
		if (packet.status != B_OK) {
			DataLost();
			continue;
		}
		if (packet.actual_length > 0) {
			fDataInput->Write(&transfer.buffer[i * transfer.packetLength],
				packet.actual_length);
		}
	}
}


//!	Waits with fIsoOrder held, up to a bound, until \a ticket is next.
bool
CamDevice::_WaitIsoTurn(int64 ticket)
{
	for (int i = 0; fIsoDelivered < ticket && i < kIsoTurnTries; i++) {
		fIsoOrder.Unlock();
		snooze(kIsoTurnWait);
		fIsoOrder.Lock();
	}
	return fIsoDelivered == ticket;
}


int32
CamDevice::_IsoPumpThread(void *_this)
{
	CamDevice *dev = (CamDevice *)_this;
	return dev->_IsoPump();
}


int32
CamDevice::_DataPumpThread(void *_this)
{
	CamDevice *dev = (CamDevice *)_this;
	return dev->DataPumpThread();
}


void
CamDevice::DumpRegs()
{
}


status_t
CamDevice::SendCommand(uint8 dir, uint8 request, uint16 value,
	uint16 index, uint16 length, void* data)
{
	ssize_t ret;
	if (!GetDevice())
		return ENODEV;
	if (length > GetDevice()->MaxEndpoint0PacketSize())
		return EINVAL;
	ret = GetDevice()->ControlTransfer(
		USB_REQTYPE_VENDOR | USB_REQTYPE_INTERFACE_OUT | dir,
		request, value, index, length, data);
	return ret;
}


CamDeviceAddon::CamDeviceAddon(WebCamMediaAddOn* webcam)
	: fWebCamAddOn(webcam),
	  fSupportedDevices(NULL)
{
}


CamDeviceAddon::~CamDeviceAddon()
{
}


const char *
CamDeviceAddon::BrandName()
{
	return "<unknown>";
}


status_t
CamDeviceAddon::Sniff(BUSBDevice *device)
{
	PRINT((CH ": Sniffing for %s" CT, BrandName()));
	if (!fSupportedDevices)
		return ENODEV;
	if (!device)
		return EINVAL;

	bool supported = false;
	for (uint32 i = 0; !supported && fSupportedDevices[i].vendor; i++) {
		if ((fSupportedDevices[i].desc.vendor != 0
			&& device->VendorID() != fSupportedDevices[i].desc.vendor)
			|| (fSupportedDevices[i].desc.product != 0
			&& device->ProductID() != fSupportedDevices[i].desc.product))
			continue;

		if ((fSupportedDevices[i].desc.dev_class == 0
			|| device->Class() == fSupportedDevices[i].desc.dev_class)
			&& (fSupportedDevices[i].desc.dev_subclass == 0
			|| device->Subclass() == fSupportedDevices[i].desc.dev_subclass)
			&& (fSupportedDevices[i].desc.dev_protocol == 0
			|| device->Protocol() == fSupportedDevices[i].desc.dev_protocol)) {
			supported = true;
		}

#ifdef __HAIKU__
		// we have to check all interfaces for matching class/subclass/protocol
		for (uint32 j = 0; !supported && j < device->CountConfigurations(); j++) {
			const BUSBConfiguration* cfg = device->ConfigurationAt(j);
			for (uint32 k = 0; !supported && k < cfg->CountInterfaces(); k++) {
				const BUSBInterface* intf = cfg->InterfaceAt(k);
				for (uint32 l = 0; !supported && l < intf->CountAlternates(); l++) {
					const BUSBInterface* alt = intf->AlternateAt(l);
					if ((fSupportedDevices[i].desc.dev_class == 0
						|| alt->Class() == fSupportedDevices[i].desc.dev_class)
						&& (fSupportedDevices[i].desc.dev_subclass == 0
						|| alt->Subclass() == fSupportedDevices[i].desc.dev_subclass)
						&& (fSupportedDevices[i].desc.dev_protocol == 0
						|| alt->Protocol() == fSupportedDevices[i].desc.dev_protocol)) {
						supported = true;
					}
				}
			}
		}
#endif

		if (supported)
			return i;
	}

	return ENODEV;
}


CamDevice *
CamDeviceAddon::Instantiate(CamRoster &roster, BUSBDevice *from)
{
	return NULL;
}


void
CamDeviceAddon::SetSupportedDevices(const usb_webcam_support_descriptor *devs)
{
	fSupportedDevices = devs;
}
