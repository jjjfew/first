/****************************************************************************
* Copyright (C) 2015 Hillcrest Laboratories, Inc.
*
* Filename:
* Date:
* Description:
*
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
*   Redistributions of source code must retain the above copyright
*   notice, this list of conditions and the following disclaimer.
*
*   Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the distribution.
*
*   Neither the name of the copyright holder nor the names of the
*   contributors may be used to endorse or promote products derived from
*   this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER
* OR CONTRIBUTORS BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
* OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
* ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* The information provided is believed to be accurate and reliable.
* The copyright holder assumes no responsibility
* for the consequences of use of such information nor for any infringement
* of patents or other rights of third parties which may result from its use.
* No license is granted by implication or otherwise under any patent or
* patent rights of the copyright holder.
**************************************************************************/

#ifndef SENSORHUB_HID_H
#define SENSORHUB_HID_H

#include "sensorhub_platform.h"
#include "sensorhub.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BNO070_MAX_INPUT_REPORT_LEN 18
#define BNO070_REGISTER_HID_DESCRIPTOR    0x01
#define BNO070_REGISTER_REPORT_DESCRIPTOR 0x02
#define BNO070_REGISTER_INPUT             0x03
#define BNO070_REGISTER_OUTPUT            0x04
#define BNO070_REGISTER_COMMAND           0x05
#define BNO070_REGISTER_DATA              0x06
#define BNO070_DESC_V1_LEN                30
#define BNO070_DESC_V1_BCD                0x0100

enum sensorhub_ReportId_e {
    SENSORHUB_PRODUCT_ID_REQUEST = 0x80,
    SENSORHUB_PRODUCT_ID_RESPONSE = 0x81,
    SENSORHUB_FRS_WRITE_REQUEST = 0x82,
    SENSORHUB_FRS_WRITE_DATA_REQUEST = 0x83,
    SENSORHUB_FRS_WRITE_RESPONSE = 0x84,
    SENSORHUB_FRS_READ_REQUEST = 0x85,
    SENSORHUB_FRS_READ_RESPONSE = 0x86
};
typedef uint8_t sensorhub_ReportId_t;

enum hid_ReportType_e {
    HID_REPORT_TYPE_INPUT = 0x10,
    HID_REPORT_TYPE_OUTPUT = 0x20,
    HID_REPORT_TYPE_FEATURE = 0x30
};
typedef uint8_t sensorhub_ReportType_t;

enum hid_ReportOpcode_e {
    HID_RESET_OPCODE = 0x01,
    HID_GET_REPORT_OPCODE = 0x02,
    HID_SET_REPORT_OPCODE = 0x03,
    HID_GET_IDLE_OPCODE = 0x04,
    HID_SET_IDLE_OPCODE = 0x05,
    HID_GET_PROTOCOL_OPCODE = 0x06,
    HID_SET_PROTOCOL_OPCODE = 0x07,
    HID_SET_POWER_OPCODE = 0x08
};

typedef struct hid_descriptor_s {
	uint16_t wHIDDescLength;
	uint16_t bcdVersion;
	uint16_t wReportDescriptorLength;
	uint16_t wReportDescriptorRegister;
	uint16_t wInputRegister;
	uint16_t wMaxInputLength;
	uint16_t wOutputRegister;
	uint16_t wMaxOutputLength;
	uint16_t wCommandRegister;
	uint16_t wDataRegister;
	uint16_t wVendorID;
	uint16_t wProductID;
	uint16_t wVersionID;
	uint8_t reserved[4];
} __packed hid_descriptor_t;

int shhid_setReport(const sensorhub_t * sh,
                    sensorhub_ReportType_t reportType,
                    sensorhub_ReportId_t reportId,
                    const uint8_t * payload, uint8_t payloadLength);

int shhid_getReport(const sensorhub_t * sh,
                    sensorhub_ReportType_t reportType,
                    sensorhub_ReportId_t reportId,
                    uint8_t * payload, uint8_t payloadLength);

#ifdef __cplusplus
}
#endif
#endif                          // SENSORHUB_HID_H
