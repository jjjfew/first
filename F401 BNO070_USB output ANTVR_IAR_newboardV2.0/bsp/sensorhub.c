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

#include "sensorhub.h"
#include "sensorhub_hid.h"
#include "i2c_master_transfer.h"
static int checkError(const sensorhub_t * sh, int rc)
{
    if (rc < 0 && sh->onError)
        sh->onError(sh, rc);

    return rc;
}

int sensorhub_i2cTransferWithRetry(const sensorhub_t * sh,
                                   uint8_t address,
                                   const uint8_t * sendData,
                                   int sendLength,
                                   uint8_t * receiveData,
                                   int receiveLength)
{
    int rc;
    int retries = 0;

    for (;;) {
        sh->stats->i2cTransfers++;
        rc = sh->i2cTransfer(sh, address, sendData, sendLength, receiveData,
                             receiveLength);
       // printf("1 times\n");
        if (rc >= 0)
            break;

        sh->stats->i2cErrors++;

        if (retries >= sh->max_retries)
            break;

        sh->stats->i2cRetries++;
        retries++;
        
        printf("retry %d times\n",retries);
    }

    return rc;
}

static int sensorhub_i2c_handshake(const sensorhub_t * sh) {

    int rc;

    //BUILD_BUG_ON(sizeof(desc) != BNO070_DESC_V1_LEN);
    uint8_t const cmd[2] = {
        BNO070_REGISTER_HID_DESCRIPTOR & 0xFF,
        (BNO070_REGISTER_HID_DESCRIPTOR >> 8) & 0xFF
    };
    union {
        hid_descriptor_t desc;
        uint8_t descBuf[sizeof(hid_descriptor_t)];
    };

    // Call I2C directly with no retries.
    rc = sh->i2cTransfer(sh, sh->sensorhubAddress, cmd, sizeof(cmd),
                                        descBuf, sizeof(descBuf));
    if (rc < 0) { printf("fail to get HID 1 time\n");
        return rc;
    }

  //  if (sh->debugPrintf) {
    //  sh->debugPrintf(
      printf( "I2C Hid Descriptor:\r\n"
            "    wHIDDescLength            = %04x\r\n"
            "    bcdVersion                = %04x\r\n"
            "    wReportDescriptorLength   = %04x\r\n"
            "    wReportDescriptorRegister = %04x\r\n"
            "    wInputRegister            = %04x\r\n"
            "    wMaxInputLength           = %04x\r\n"
            "    wOutputRegister           = %04x\r\n"
            "    wMaxOutputLength          = %04x\r\n"
            "    wCommandRegister          = %04x\r\n"
            "    wDataRegister             = %04x\r\n"
            "    wVendorID                 = %04x\r\n"
            "    wProductID                = %04x\r\n"
            "    wVersionID                = %04x\r\n\n"
            , (unsigned int) desc.wHIDDescLength
            , (unsigned int) desc.bcdVersion
            , (unsigned int) desc.wReportDescriptorLength
            , (unsigned int) desc.wReportDescriptorRegister
            , (unsigned int) desc.wInputRegister
            , (unsigned int) desc.wMaxInputLength
            , (unsigned int) desc.wOutputRegister
            , (unsigned int) desc.wMaxOutputLength
            , (unsigned int) desc.wCommandRegister
            , (unsigned int) desc.wDataRegister
            , (unsigned int) desc.wVendorID
            , (unsigned int) desc.wProductID
            , (unsigned int) desc.wVersionID
        );
    //}
   //osDelay(500);
    if (desc.wHIDDescLength != BNO070_DESC_V1_LEN) {
        return checkError(sh, SENSORHUB_STATUS_INVALID_HID_DESCRIPTOR);
    }

    if (desc.bcdVersion != BNO070_DESC_V1_BCD) {
        return checkError(sh, SENSORHUB_STATUS_INVALID_HID_DESCRIPTOR);
    }

    return SENSORHUB_STATUS_SUCCESS;
}

static int sensorhub_probe_internal(const sensorhub_t * sh, bool reset)
{
    uint8_t data[2];
    int i;
    int rc;
  
    if (reset) {
        bool host_intn_pulled_low;
  
        /* Put the BNO070 into reset */
       sh->setRSTN(sh, 0);

        /* BNO070 BOOTN high (no bootloader mode) */
       sh->setBOOTN(sh, 1);
        //printf("set boot=1\n");
        //HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, 1);
        
        /* Let the BNO sit in reset for a little while */
        sh->delay(sh, 10);
        
        /* Check if HOST_INTN is incorrectly pulled low. */
        host_intn_pulled_low = !sh->getHOST_INTN(sh);

        /* Take the BNO070 out of reset */
        sh->setRSTN(sh, 1);

        if (host_intn_pulled_low) {
            /* If HOST_INTN is pulled low, wait for the BNO to set
             * it high. Once the BNO inits HOST_INTN high, it leaves
             * it high for 8-10 ms before it's ready for commands.
             * Wait for a max of 100 ms, since the BNO will surely be
             * ready by then and we probably missed the short time
             * that it wasn't.
             */
          printf("int=0 first time\n");
            for (i = 0; i < 100; i++) {
                if (sh->getHOST_INTN(sh))
                    break;
                sh->delay(sh, 1);
            }
        }
    }
   sh->delay(sh, 100);  //这里必须要延时，要不然下面会重复尝试多次
    /* Wait for the BNO070 to boot and check the HID descriptor */
    for (i = 0; i < 100; i++) {  //如果不成功，重复尝试
        rc = sensorhub_i2c_handshake(sh);
        if (rc != SENSORHUB_STATUS_ERROR_I2C_IO) break;  //不能success,则返回ERROR_I2C
        sh->delay(sh, 10);      //thjf
    }
    if (rc < 0) {
        return checkError(sh, rc);
    }

    if (!sh->getHOST_INTN(sh)) {    //如果int=0,则通过空读来释放INT脚
      printf("int=0\n");
        /* Clear the interrupt by reading 2x */
        for (i = 0; i < 2; i++) {       
            int rc =
                sensorhub_i2cTransferWithRetry(sh, sh->sensorhubAddress, NULL, 0, data,
                                               sizeof(data));
            if (rc != SENSORHUB_STATUS_SUCCESS) {
                /* Error trying to communicate with the BNO070 */
                return checkError(sh, SENSORHUB_STATUS_RESET_FAIL_2);
            }
        }

        /* Check that the interrupt was cleared. */
        if (!sh->getHOST_INTN(sh)) {
            /* Not expecting HOST_INTN. It should have been cleared. */
            return checkError(sh, SENSORHUB_STATUS_RESET_INTN_BROKE);
        }
    }

    /* We're ready to go. */
    return SENSORHUB_STATUS_SUCCESS;
}

int sensorhub_probe(const sensorhub_t * sh) {
  return sensorhub_probe_internal(sh, true);
}


static inline uint16_t read16(const uint8_t * buffer)
{
    return ((uint16_t) buffer[0]) | ((uint16_t) (buffer[1]) << 8);
}

static inline void write16(uint8_t * buffer, uint16_t value)
{
    buffer[0] = (uint8_t) (value);       //低位
    buffer[1] = (uint8_t) (value >> 8);  
}

static inline uint32_t read32(const uint8_t * buffer)
{
    return ((uint32_t) buffer[0]) |
           ((uint32_t) (buffer[1]) << 8) |
           ((uint32_t) (buffer[2]) << 16) | ((uint32_t) (buffer[3]) << 24);
}

static inline uint32_t read32be(const uint8_t * buffer)
{
    return ((uint32_t) buffer[3]) |
           ((uint32_t) (buffer[2]) << 8) |
           ((uint32_t) (buffer[1]) << 16) | ((uint32_t) (buffer[0]) << 24);
}

static inline void write32(uint8_t * buffer, uint32_t value)
{
    buffer[0] = (uint8_t) (value);
    buffer[1] = (uint8_t) (value >> 8);
    buffer[2] = (uint8_t) (value >> 16);
    buffer[3] = (uint8_t) (value >> 24);
}

int sensorhub_setDynamicFeature(const sensorhub_t * sh,
                                sensorhub_Sensor_t sensor,
                                const sensorhub_SensorFeature_t * settings)
{
    uint8_t payload[11];

    payload[0] = (settings->changeSensitivityRelative ? 0x1 : 0x0) |
                 (settings->changeSensitivityEnabled ? 0x2 : 0x0) |
                 (settings->wakeupEnabled ? 0x4 : 0x0);
    write16(&payload[1], settings->changeSensitivity);
    write32(&payload[3], settings->reportInterval);
    write32(&payload[7], settings->batchInterval);
    return checkError(sh, shhid_setReport(sh,
                                          HID_REPORT_TYPE_FEATURE,
                                          sensor,
                                          payload, sizeof(payload)));
}

int sensorhub_getDynamicFeature(const sensorhub_t * sh,
                                sensorhub_Sensor_t sensor,
                                sensorhub_SensorFeature_t * settings)
{
    uint8_t payload[11];
    int rc;

    rc = shhid_getReport(sh,
                         HID_REPORT_TYPE_FEATURE,
                         sensor, payload, sizeof(payload));
    if (rc != SENSORHUB_STATUS_SUCCESS)
        return checkError(sh, rc);

    settings->changeSensitivityRelative = ((payload[0] & 0x1) != 0);
    settings->changeSensitivityEnabled = ((payload[0] & 0x2) != 0);
    settings->wakeupEnabled = ((payload[0] & 0x4) != 0);

    settings->changeSensitivity = read16(&payload[1]);
    settings->reportInterval = read32(&payload[3]);
    settings->batchInterval = read32(&payload[7]);
    
    //printf("%02d\n",settings->changeSensitivityRelative);
    int p;
    printf("read feature report\n");
    for(p=0;p<11;p++)printf("%02x ",payload[p]); printf("\n"); 
    return SENSORHUB_STATUS_SUCCESS;
}

static int sensorhub_decodeEvent(const sensorhub_t * sh,
                                 const uint8_t * report,
                                 sensorhub_Event_t * event)
{
    /* Get the length. Technically, it's 2 bytes, but valid reports won't come
       close to using the 2nd byte */
    int length = report[0];
    if (length > BNO070_MAX_INPUT_REPORT_LEN || report[1] != 0)
    {
      printf("report too long= %2x\n",length);  
      return checkError(sh, SENSORHUB_STATUS_REPORT_LEN_TOO_LONG);
    }  
    /* Fill out common fields */
    event->sensor = report[2];
    event->sequenceNumber = report[3];
    event->status = report[4];
    event->delay = report[5];

    switch (event->sensor) {
        /* Reports that are 1 16-bit integer */
    case SENSORHUB_HUMIDITY:
    case SENSORHUB_PROXIMITY:
    case SENSORHUB_TEMPERATURE:
    case SENSORHUB_SIGNIFICANT_MOTION:
    case SENSORHUB_SHAKE_DETECTOR:
    case SENSORHUB_FLIP_DETECTOR:
    case SENSORHUB_PICKUP_DETECTOR:
    case SENSORHUB_STEP_DETECTOR:
    case SENSORHUB_STABILITY_DETECTOR:
        if (length < 8)
            return checkError(sh, SENSORHUB_STATUS_REPORT_INVALID_LEN);

        event->un.field16[0] = read16(&report[6]);
        break;

        /* Reports that are 1 32-bit integer */
    case SENSORHUB_PRESSURE:
    case SENSORHUB_AMBIENT_LIGHT:
        if (length < 10)
            return checkError(sh, SENSORHUB_STATUS_REPORT_INVALID_LEN);

        event->un.field32[0] = read32(&report[6]);
        break;

        /* 4 16-bit integers and a 32-bit timestamp */
    case SENSORHUB_RAW_ACCELEROMETER:
    case SENSORHUB_RAW_GYROSCOPE:
    case SENSORHUB_RAW_MAGNETOMETER:
        if (length < 18)
            return checkError(sh, SENSORHUB_STATUS_REPORT_INVALID_LEN);

        event->un.field16[0] = read16(&report[6]);
        event->un.field16[1] = read16(&report[8]);
        event->un.field16[2] = read16(&report[10]);
        event->un.field16[3] = read16(&report[12]);
        event->un.field32[2] = read32(&report[14]);
        break;

        /* Reports that are 3 16-bit integers */
    case SENSORHUB_ACCELEROMETER:                                           //length=12
    case SENSORHUB_LINEAR_ACCELERATION:
    case SENSORHUB_GRAVITY:
    case SENSORHUB_GYROSCOPE_CALIBRATED:
    case SENSORHUB_MAGNETIC_FIELD_CALIBRATED:
        if (length < 12)
            return checkError(sh, SENSORHUB_STATUS_REPORT_INVALID_LEN);

        event->un.field16[0] = read16(&report[6]);
        event->un.field16[1] = read16(&report[8]);
        event->un.field16[2] = read16(&report[10]);
        break;

        /* Reports that are 4 16-bit integers */
    case SENSORHUB_GAME_ROTATION_VECTOR:
        if (length < 16)
            return checkError(sh, SENSORHUB_STATUS_REPORT_INVALID_LEN);

        event->un.field16[0] = read16(&report[6]);
        event->un.field16[1] = read16(&report[8]);
        event->un.field16[2] = read16(&report[10]);
        event->un.field16[3] = read16(&report[12]);
        break;

        /* Reports that are 5 16-bit integers */
    case SENSORHUB_ROTATION_VECTOR: 
    case SENSORHUB_GEOMAGNETIC_ROTATION_VECTOR:
        if (length < 16)
            return checkError(sh, SENSORHUB_STATUS_REPORT_INVALID_LEN);
        
        event->un.field16[0] = read16(&report[6]);
        event->un.field16[1] = read16(&report[8]);
        event->un.field16[2] = read16(&report[10]);
        event->un.field16[3] = read16(&report[12]);
        event->un.field16[4] = read16(&report[14]);
        break;

        /* Reports that are 6 16-bit integers */
    case SENSORHUB_GYROSCOPE_UNCALIBRATED:
    case SENSORHUB_MAGNETIC_FIELD_UNCALIBRATED:
        if (length < 16)
            return checkError(sh, SENSORHUB_STATUS_REPORT_INVALID_LEN);

        event->un.field16[0] = read16(&report[6]);
        event->un.field16[1] = read16(&report[8]);
        event->un.field16[2] = read16(&report[10]);
        event->un.field16[3] = read16(&report[12]);
        event->un.field16[4] = read16(&report[14]);
        event->un.field16[5] = read16(&report[16]);
        break;

    case SENSORHUB_STEP_COUNTER:
        if (length < 14)
            return checkError(sh, SENSORHUB_STATUS_REPORT_INVALID_LEN);

        event->un.stepCounter.detectLatency = read32(&report[6]);
        event->un.stepCounter.steps = read16(&report[10]);
        event->un.stepCounter.reserved = read16(&report[12]);
        break;

    case SENSORHUB_PRODUCT_ID_RESPONSE:
    case SENSORHUB_FRS_READ_RESPONSE:
    case SENSORHUB_FRS_WRITE_RESPONSE:
        /* Stale FRS read or write responses */
        /* NOTE: Don't run these through checkError, since they are such a
         *       minor annoyance that we don't want to alert the user. The
         *       calling code can do more if it wants.
         */
        return SENSORHUB_STATUS_NOT_AN_EVENT;

        /* TBD */
    case SENSORHUB_SAR:
    case SENSORHUB_TAP_DETECTOR:
    case SENSORHUB_ACTIVITY_CLASSIFICATION:
    default:
        return checkError(sh, SENSORHUB_STATUS_REPORT_UNKNOWN);
    }
    return SENSORHUB_STATUS_SUCCESS;
}

static int sensorhub_pollForReport(const sensorhub_t * sh,
                                   uint8_t * report)
{
    int rc;

    /* Check HOST_INTN to see if there are events waiting. */
    if (sh->getHOST_INTN(sh))
    {
        //printf("no int=0 generated\n");
        return checkError(sh, SENSORHUB_STATUS_NO_REPORT_PENDING);
    }
    rc = sensorhub_i2cTransferWithRetry(sh, sh->sensorhubAddress, NULL, 0, report,
                                        BNO070_MAX_INPUT_REPORT_LEN);
    return checkError(sh, rc);
}

int sensorhub_poll(const sensorhub_t * sh, sensorhub_Event_t * events,
                   int maxEvents, int *numEvents)
{
    uint8_t report[BNO070_MAX_INPUT_REPORT_LEN];   //max =18
    int rc;

    *numEvents = 0;

    do {
        rc = sensorhub_pollForReport(sh, report);      //检测如INT=0,则retry读下BNO
        if (rc == SENSORHUB_STATUS_NO_REPORT_PENDING)
            return SENSORHUB_STATUS_SUCCESS;
        else if (rc != SENSORHUB_STATUS_SUCCESS)
            return checkError(sh, rc);

        /* Null length reports also indicate no more events */
        if (report[0] == 0)                             //头两个字节代表长度
            return SENSORHUB_STATUS_SUCCESS;           

        /* Decode the event. Ignore reports that aren't events. */
        rc = sensorhub_decodeEvent(sh, report, &events[*numEvents]);  
        if (rc == SENSORHUB_STATUS_NOT_AN_EVENT)
            continue;
        else if (rc != SENSORHUB_STATUS_SUCCESS)
            return checkError(sh, rc);

        (*numEvents)++;

        /* Allow hub time to release host interrupt */
       //sh->delay(sh, 10);
        sh->delay(sh, 1);
    } while (*numEvents < maxEvents);                               //连续5次

    /* We ran out of places to store events, so return. */
    return SENSORHUB_STATUS_MORE_EVENTS_PENDING;
}

int sensorhub_waitForEvent(const sensorhub_t * sh,
                           sensorhub_Event_t * event, uint32_t timeout)
{
    uint32_t startTime = sh->getTick(sh);
    int rc;
    int numEvents;
    do {
        rc = sensorhub_poll(sh, event, 1, &numEvents);
        /* Keep trying even if there are warnings */
    } while (rc >= 0 && numEvents == 0
             && (sh->getTick(sh) - startTime < timeout));

    return rc;
}

static int sensorhub_waitForReport(const sensorhub_t * sh,
                                   uint8_t * report, uint32_t timeout)
{
    uint32_t startTime = sh->getTick(sh);
    int rc;
    do {
        rc = sensorhub_pollForReport(sh, report);
    } while (rc == SENSORHUB_STATUS_NO_REPORT_PENDING
             && (sh->getTick(sh) - startTime < timeout));

    return rc;
}

int sensorhub_flushEvents(const sensorhub_t * sh)
{
    uint8_t report[BNO070_MAX_INPUT_REPORT_LEN];
    int rc;
    do {
        rc = sensorhub_pollForReport(sh, report);
    } while (rc == SENSORHUB_STATUS_SUCCESS);

    if (rc == SENSORHUB_STATUS_NO_REPORT_PENDING)
        return SENSORHUB_STATUS_SUCCESS;
    else
        return rc;
}

static int sensorhub_sendProductIDRequest(const sensorhub_t * sh)
{
    uint8_t payload[1];

    payload[0] = 0;
    return shhid_setReport(sh,
                           HID_REPORT_TYPE_OUTPUT,                     //0x20
                           SENSORHUB_PRODUCT_ID_REQUEST,
                           payload, sizeof(payload));
}

static int sensorhub_sendFRSReadRequest(const sensorhub_t * sh,
                                        sensorhub_FRS_t recordType,
                                        uint16_t offset, uint16_t length)
{
    uint8_t payload[7];

    payload[0] = 0;
    write16(&payload[1], offset);
    write16(&payload[3], recordType);
    write16(&payload[7], length);
    return shhid_setReport(sh,
                           HID_REPORT_TYPE_OUTPUT,
                           SENSORHUB_FRS_READ_REQUEST,
                           payload, sizeof(payload));
}

static int sensorhub_sendFRSWriteRequest(const sensorhub_t * sh,
        sensorhub_FRS_t recordType,
        uint16_t length)
{
    uint8_t payload[5];

    payload[0] = 0;
    write16(&payload[1], length);
    write16(&payload[3], recordType);
    return shhid_setReport(sh,
                           HID_REPORT_TYPE_OUTPUT,
                           SENSORHUB_FRS_WRITE_REQUEST,
                           payload, sizeof(payload));
}

static int sensorhub_sendFRSWriteDataRequest(const sensorhub_t * sh,
        uint16_t offset,
        const uint32_t * data,
        uint8_t length)
{
    uint8_t payload[11];

    payload[0] = 0;
    write16(&payload[1], offset);
    write32(&payload[3], data[0]);
    write32(&payload[7], length > 1 ? data[1] : 0);
    return shhid_setReport(sh,
                           HID_REPORT_TYPE_OUTPUT,
                           SENSORHUB_FRS_WRITE_DATA_REQUEST,
                           payload, sizeof(payload));
}



static int sensorhub_getFRSReadResponse(const sensorhub_t * sh,
                                        sensorhub_FRSReadResponse_t *
                                        response, uint32_t timeout)
{
    uint32_t startTime = sh->getTick(sh);
    uint32_t now = startTime;
    uint8_t payload[BNO070_MAX_INPUT_REPORT_LEN];
    while (now - startTime < timeout) {
        int rc = sensorhub_waitForReport(sh, payload,
                                         timeout - (now - startTime));
        if (rc != SENSORHUB_STATUS_SUCCESS)
            return rc;

        if (payload[0] == 18 && payload[1] == 0
                && payload[2] == SENSORHUB_FRS_READ_RESPONSE) {
            response->length = (payload[3] >> 4);
            response->status = (payload[3] & 0xf);
            response->offset = read16(&payload[4]);
            response->data[0] = read32(&payload[6]);
            response->data[1] = read32(&payload[10]);
            response->recordType = read16(&payload[14]);
            response->reserved = read16(&payload[16]);

            return SENSORHUB_STATUS_SUCCESS;
        }

        now = sh->getTick(sh);
    }
    return SENSORHUB_STATUS_NO_REPORT_PENDING;
}

static int sensorhub_getFRSWriteResponse(const sensorhub_t * sh,
        sensorhub_FRSWriteResponse_t *
        response, uint32_t timeout)
{
    uint32_t startTime = sh->getTick(sh);
    uint32_t now = startTime;
    uint8_t payload[BNO070_MAX_INPUT_REPORT_LEN];
    while (now - startTime < timeout) {
        int rc = sensorhub_waitForReport(sh, payload,
                                         timeout - (now - startTime));
        if (rc != SENSORHUB_STATUS_SUCCESS)
            return rc;

        if (payload[0] == 6 && payload[1] == 0
                && payload[2] == SENSORHUB_FRS_WRITE_RESPONSE) {
            response->status = payload[3];
            response->offset = read16(&payload[4]);
            return SENSORHUB_STATUS_SUCCESS;
        }
        now = sh->getTick(sh);
    }
    return SENSORHUB_STATUS_NO_REPORT_PENDING;
}

static int sensorhub_getProductIDResponse(const sensorhub_t * sh,
        sensorhub_ProductID_t * response,
        uint32_t timeout)
{
    uint32_t startTime = sh->getTick(sh);
    uint32_t now = startTime;
    uint8_t payload[BNO070_MAX_INPUT_REPORT_LEN];
    while (now - startTime < timeout) {
        int rc = sensorhub_waitForReport(sh, payload,
                                         timeout - (now - startTime));
        if (rc != SENSORHUB_STATUS_SUCCESS)
            return rc;

        if (payload[0] == 18 && payload[1] == 0
                && payload[2] == SENSORHUB_PRODUCT_ID_RESPONSE) {
            response->resetCause = payload[3];
            response->swVersionMajor = payload[4];
            response->swVersionMinor = payload[5];
            response->swPartNumber = read32(&payload[6]);
            response->swBuildNumber = read32(&payload[10]);
            response->swVersionPatch = read16(&payload[14]);
            return SENSORHUB_STATUS_SUCCESS;
        }
        now = sh->getTick(sh);
    }
    return SENSORHUB_STATUS_NO_REPORT_PENDING;
}

int sensorhub_readFRS(const sensorhub_t * sh, sensorhub_FRS_t recordType,
                      uint32_t * data, uint16_t offset, uint16_t maxLength,
                      uint16_t * actualLength)
{
    sensorhub_FRSReadResponse_t resp;
    int rc;
    int i;

    *actualLength = 0;

    rc = sensorhub_sendFRSReadRequest(sh, recordType, offset, maxLength);
    if (rc != SENSORHUB_STATUS_SUCCESS)
        return checkError(sh, rc);

    while (*actualLength < maxLength) {
        rc = sensorhub_getFRSReadResponse(sh, &resp, 1000);
        if (rc != SENSORHUB_STATUS_SUCCESS)
            return rc;

        switch (resp.status) {
        case SENSORHUB_FRP_RD_NO_ERR:
            if (resp.offset != offset)
                return SENSORHUB_STATUS_FRS_READ_BAD_OFFSET;
            if (resp.length > 2)
                return SENSORHUB_STATUS_FRS_READ_BAD_LENGTH;
            if (resp.recordType != recordType)
                return SENSORHUB_STATUS_FRS_READ_BAD_TYPE;

            for (i = 0; i < resp.length && *actualLength < maxLength; i++) {
                *data++ = resp.data[i];
                (*actualLength)++;
                offset++;
            }
            break;

        case SENSORHUB_FRP_RD_BAD_TYPE:
            return checkError(sh,
                              SENSORHUB_STATUS_FRS_READ_UNRECOGNIZED_FRS);

        case SENSORHUB_FRP_RD_BUSY:
            return checkError(sh, SENSORHUB_STATUS_FRS_READ_BUSY);

        case SENSORHUB_FRP_RD_BAD_OFFSET:
            return checkError(sh,
                              SENSORHUB_STATUS_FRS_READ_OFFSET_OUT_OF_RANGE);

        case SENSORHUB_FRP_RD_RECORD_EMPTY:
            return checkError(sh, SENSORHUB_STATUS_FRS_READ_EMPTY);

        case SENSORHUB_FRP_RD_COMPLETE:
        case SENSORHUB_FRP_RD_BLOCK_DONE:
        case SENSORHUB_FRP_RD_BLOCK_REC_DONE:
            if (resp.offset != offset)
                return checkError(sh,
                                  SENSORHUB_STATUS_FRS_READ_BAD_OFFSET);
            if (resp.length > 2)
                return checkError(sh,
                                  SENSORHUB_STATUS_FRS_READ_BAD_LENGTH);
            if (resp.recordType != recordType)
                return checkError(sh, SENSORHUB_STATUS_FRS_READ_BAD_TYPE);

            for (i = 0; i < resp.length && *actualLength < maxLength; i++) {
                *data++ = resp.data[i];
                (*actualLength)++;
                offset++;
            }
            return SENSORHUB_STATUS_SUCCESS;

        case SENSORHUB_FRP_RD_DEVICE_ERROR:
            return checkError(sh, SENSORHUB_STATUS_FRS_READ_DEVICE_ERROR);

        default:
            return checkError(sh, SENSORHUB_STATUS_FRS_READ_UNKNOWN_ERROR);
        }
    }

    /* Since we always specify the number of bytes that we want, it's unexpected
       to get too many */
    return checkError(sh, SENSORHUB_STATUS_FRS_READ_UNEXPECTED_LENGTH);
}

int sensorhub_writeFRS(const sensorhub_t * sh, sensorhub_FRS_t recordType,
                       const uint32_t * data, uint16_t length)
{
    sensorhub_FRSWriteResponse_t resp;
    int rc;
    uint16_t offset = 0;

    /* Step 1: Make a write request */
    rc = sensorhub_sendFRSWriteRequest(sh, recordType, length);   //retry 5 times
    if (rc != SENSORHUB_STATUS_SUCCESS)
        return checkError(sh, rc);

    rc = sensorhub_getFRSWriteResponse(sh, &resp, 1000);          //retry 5 times,申请写的返回
    if (rc != SENSORHUB_STATUS_SUCCESS)
        return checkError(sh, rc);

    switch (resp.status) {
    case SENSORHUB_FRP_WR_READY:
        /* Expected response for length > 0 */
        break;

    case SENSORHUB_FRP_WR_COMPLETE:
        /* Expected response for length == 0. No validation step needed. */
        return SENSORHUB_STATUS_SUCCESS;

    case SENSORHUB_FRP_WR_BAD_TYPE:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_TYPE);

    case SENSORHUB_FRP_WR_BUSY:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BUSY);

    case SENSORHUB_FRP_WR_BAD_LENGTH:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_LENGTH);

    case SENSORHUB_FRP_WR_BAD_MODE:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_MODE);

    case SENSORHUB_FRP_WR_DEVICE_ERROR:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_DEVICE_ERROR);

    case SENSORHUB_FRP_WR_READONLY:
        return checkError(sh, SENSORHUB_STATUS_FRS_READ_ONLY);

    case SENSORHUB_FRP_WR_FAILED:
    case SENSORHUB_FRP_WR_REC_VALID:
    case SENSORHUB_FRP_WR_REC_INVALID:
    case SENSORHUB_FRP_WR_ACK:
    default:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_STATUS);
    }

    /* Step 2: Send the write data requests */
    while (length > 0) {                                                        //每次发2组数据（每组4个字节）
        uint8_t towrite = (length > 2) ? 2 : length;   //4>2,towrite=2
        rc = sensorhub_sendFRSWriteDataRequest(sh, offset, data, towrite);
        if (rc != SENSORHUB_STATUS_SUCCESS)
            return checkError(sh, rc);

        rc = sensorhub_getFRSWriteResponse(sh, &resp, 1000);
        if (rc != SENSORHUB_STATUS_SUCCESS)
            return checkError(sh, rc);
        switch (resp.status) {
        case SENSORHUB_FRP_WR_ACK:
            length -= towrite;
            data += towrite;
            offset += towrite;
            break;

        case SENSORHUB_FRP_WR_BAD_TYPE:
            return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_TYPE);

        case SENSORHUB_FRP_WR_BUSY:
            return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BUSY);

        case SENSORHUB_FRP_WR_BAD_LENGTH:
            return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_LENGTH);

        case SENSORHUB_FRP_WR_BAD_MODE:
            return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_MODE);

        case SENSORHUB_FRP_WR_DEVICE_ERROR:
            return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_DEVICE_ERROR);

        case SENSORHUB_FRP_WR_FAILED:
            return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_FAILED);

        case SENSORHUB_FRP_WR_READONLY:
            return checkError(sh, SENSORHUB_STATUS_FRS_READ_ONLY);

        case SENSORHUB_FRP_WR_REC_INVALID:
        case SENSORHUB_FRP_WR_REC_VALID:
        case SENSORHUB_FRP_WR_COMPLETE:
        case SENSORHUB_FRP_WR_READY:
        default:
            return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_STATUS);
        }
    }

    /* Step 3: Wait for the verification response */
    rc = sensorhub_getFRSWriteResponse(sh, &resp, 1000);
    if (rc != SENSORHUB_STATUS_SUCCESS)
        return checkError(sh, rc);
    switch (resp.status) {
    case SENSORHUB_FRP_WR_REC_VALID:
        /* Expected response */
        break;

    case SENSORHUB_FRP_WR_REC_INVALID:
        return checkError(sh, SENSORHUB_STATUS_FRS_INVALID_RECORD);

    case SENSORHUB_FRP_WR_DEVICE_ERROR:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_DEVICE_ERROR);

    case SENSORHUB_FRP_WR_FAILED:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_FAILED);

    case SENSORHUB_FRP_WR_READONLY:
    case SENSORHUB_FRP_WR_COMPLETE:
    case SENSORHUB_FRP_WR_BAD_TYPE:
    case SENSORHUB_FRP_WR_BUSY:
    case SENSORHUB_FRP_WR_BAD_LENGTH:
    case SENSORHUB_FRP_WR_BAD_MODE:
    case SENSORHUB_FRP_WR_ACK:
    case SENSORHUB_FRP_WR_READY:
    default:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_STATUS);
    }

    /* Step 4: Wait for the write complete response */
    rc = sensorhub_getFRSWriteResponse(sh, &resp, 1000);
    if (rc != SENSORHUB_STATUS_SUCCESS)
        return checkError(sh, rc);
    switch (resp.status) {
    case SENSORHUB_FRP_WR_COMPLETE:
        /* Expected response */
        break;

    case SENSORHUB_FRP_WR_DEVICE_ERROR:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_DEVICE_ERROR);

    case SENSORHUB_FRP_WR_FAILED:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_FAILED);

    case SENSORHUB_FRP_WR_REC_INVALID:
    case SENSORHUB_FRP_WR_REC_VALID:
    case SENSORHUB_FRP_WR_READONLY:
    case SENSORHUB_FRP_WR_BAD_TYPE:
    case SENSORHUB_FRP_WR_BUSY:
    case SENSORHUB_FRP_WR_BAD_LENGTH:
    case SENSORHUB_FRP_WR_BAD_MODE:
    case SENSORHUB_FRP_WR_ACK:
    case SENSORHUB_FRP_WR_READY:
    default:
        return checkError(sh, SENSORHUB_STATUS_FRS_WRITE_BAD_STATUS);
    }

    return SENSORHUB_STATUS_SUCCESS;
}

int sensorhub_getProductID(const sensorhub_t * sh,
                           sensorhub_ProductID_t * pid)
{
    int rc;
    rc = sensorhub_sendProductIDRequest(sh);   //调用retry次发命令
    if (rc != SENSORHUB_STATUS_SUCCESS)
        return checkError(sh, rc);

    rc = sensorhub_getProductIDResponse(sh, pid, 1000);   //调用retry次读
    if (rc != SENSORHUB_STATUS_SUCCESS)
        return checkError(sh, rc);
#if 1
    // Ignore the rest of the reports                    //接下来会有三组数据发送
    sensorhub_ProductID_t ignore;
    sensorhub_getProductIDResponse(sh, &ignore, 100);
    sensorhub_getProductIDResponse(sh, &ignore, 100);
    sensorhub_getProductIDResponse(sh, &ignore, 100);
#endif
    return rc;
}

int sensorhub_dfu(const sensorhub_t *sh,
                  const uint8_t *dfuStream,
                  int length)
{
    /* Check that the type of DFU stream is one that we can handle
     * NOTE: We only support one format for the BNO070 and that's type 0x01010101
     */
    uint32_t dfuFormat = read32(dfuStream);
    if (dfuFormat != 0x01010101)
        return checkError(sh, SENSORHUB_STATUS_UNEXPECTED_DFU_STREAM_TYPE);

    /* The DFU stream format is specified as big endian. Read the application
     * size to double check the length.
     */
    uint32_t applicationSize = read32be(&dfuStream[4]);
    int packetPayloadSize = dfuStream[10];
    int packetSize = packetPayloadSize + 2;

    int expectedLength = 4 /* dfu format */ +
                         6 /* application size + CRC */ +
                         3 /* packet size + CRC */ +
                         applicationSize + /* application bytes */
                         ((applicationSize + packetPayloadSize - 1) / packetPayloadSize) * 2; /* CRC per packet */
    if (expectedLength != length)
        return checkError(sh, SENSORHUB_STATUS_DFU_STREAM_SIZE_WRONG);

    /* Put the BNO070 into reset */
    sh->setRSTN(sh, 0);

    /* BNO070 BOOTN low (bootloader mode) */
    sh->setBOOTN(sh, 0);

    sh->delay(sh, 10);

    /* Take the BNO070 out of reset */
    sh->setRSTN(sh, 1);

    sh->delay(sh, 10);

    /* Send each packet of the DFU */
    int index = 4;
    while (index < length) {
        int rc;
        uint8_t response;

        int lengthToWrite = packetSize;
        if (index == 4)
            lengthToWrite = 6; // First packet -> total length
        else if (index == 10)
            lengthToWrite = 3; // Second packet -> packet length
        else if (index + lengthToWrite > length)
            lengthToWrite = length - index; // Last packet -> could be short

        rc = sensorhub_i2cTransferWithRetry(sh, sh->bootloaderAddress, &dfuStream[index], lengthToWrite, 0, 0);
        if (rc != SENSORHUB_STATUS_SUCCESS)
            return checkError(sh, rc);

        rc = sensorhub_i2cTransferWithRetry(sh, sh->bootloaderAddress, 0, 0, &response, sizeof(response));
        if (rc != SENSORHUB_STATUS_SUCCESS)
            return checkError(sh, rc);

        /* Check that we got a successful response. */
        if (response != 's')
            return checkError(sh, SENSORHUB_STATUS_DFU_RECEIVED_NAK);

        /* The capture from the Bosch programmer had a 1-3 ms delay between packets.
         * Testing confirms that if we don't delay that the programmed image doesn't
         * work. 2 ms works. 1 ms doesn't.
         */
        //sh->delay(sh, 2);

        index += lengthToWrite;
    }

    return sensorhub_probe_internal(sh, false);
}

