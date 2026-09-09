/*
* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Changes from Qualcomm Technologies, Inc. are provided under the following license:
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*
*/

#define LOG_TAG "PAL: libsession_config_utils"

#ifdef PAL_USE_SYSLOG
#include <syslog.h>
#define ALOGE(fmt, arg...) syslog (LOG_ERR, fmt, ##arg)
#define ALOGI(fmt, arg...) syslog (LOG_INFO, fmt, ##arg)
#define ALOGD(fmt, arg...) syslog (LOG_DEBUG, fmt, ##arg)
#define ALOGV(fmt, arg...) syslog (LOG_NOTICE, fmt, ##arg)
#else
#include <log/log.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef PAL_CUTILS_SUPPORTED
#include <cutils/properties.h>
#endif
#include <kvh2xml.h>
#include <agm/agm_api.h>
#include "PluginManagerIntf.h"
#include "ResourceManager.h"
#include "PayloadBuilder.h"
#include "SessionAlsaPcm.h"
#include "SessionAlsaUtils.h"
#include "ConfigSessionUtils.h"
#include "apm_api.h"
#include <fcntl.h>
//#include <sys/stat.h>
#include <sys/klog.h>        /* Definition of SYSLOG_* constants */
#include <time.h>


#ifndef DUMP_OUT_PATH
#define DUMP_OUT_PATH "/data/vendor/audio/"
#endif //DUMP_OUT_PATH

#define MAX_DUMP_FILENAME_SIZE 255
#define REGDUMP_OUT_SIZE 256*1024
#define SYSLOG_ACTION_READ_ALL 3
#define SYSLOG_ACTION_SIZE_BUFFER 10
#define TIMESTAMP_FORMAT_STRING "_%Y_%m_%d_%H_%M_%S"

#define BOLERO_PROC_INTF    "/proc/lpass_cdc_reginfo/lpass_cdc_regdump"
#define WCD939X_PROC_INTF   "/proc/wcd939x_reginfo/wcd939x_regdump"
#define WSA884X_1_PROC_INTF "/proc/wsa884x_reginfo_1/wsa884x_regdump"
#define WSA884X_2_PROC_INTF "/proc/wsa884x_reginfo_2/wsa884x_regdump"
#define WSA883X_1_PROC_INTF "/proc/wsa883x_reginfo_1/wsa883x_regdump"
#define WSA883X_2_PROC_INTF "/proc/wsa883x_reginfo_2/wsa883x_regdump"
#define WSA_SWR_PROC_INTF  "/proc/wsa_swr_ctrl/swr_mstr_ctrl_regdump"
#define WSA2_SWR_PROC_INTF "/proc/wsa2_swr_ctrl/swr_mstr_ctrl_regdump"
#define VA_SWR_PROC_INTF   "/proc/va_swr_ctrl/swr_mstr_ctrl_regdump"
#define RX_SWR_PROC_INTF   "/proc/rx_swr_ctrl/swr_mstr_ctrl_regdump"

#define KMSG_FILE     "kernel_log"
#define SILENCE_EVENT_INFO DUMP_OUT_PATH "silence_event_info"
#define BOLERO_REGDUMP_OUT_FILE          "lpass_cdc_regdump"
#define WCD939X_REGDUMP_OUT_FILE         "wcd939x_regdump"
#define VA_SWR_REGDUMP_OUT_FILE          "va_swr_regdump"

#define KMSG_OUT_FILE              DUMP_OUT_PATH KMSG_FILE TIMESTAMP_FORMAT_STRING
#define SILENCE_EVENT_INFO         DUMP_OUT_PATH "silence_event_info" TIMESTAMP_FORMAT_STRING
#define BOLERO_REGDUMP_OUT_PATH    DUMP_OUT_PATH BOLERO_REGDUMP_OUT_FILE TIMESTAMP_FORMAT_STRING
#define WCD939X_REGDUMP_OUT_PATH   DUMP_OUT_PATH WCD939X_REGDUMP_OUT_FILE TIMESTAMP_FORMAT_STRING
#define VA_SWR_REGDUM_OUT_PATH     DUMP_OUT_PATH VA_SWR_REGDUMP_OUT_FILE TIMESTAMP_FORMAT_STRING

bool silenceEventRegistered = false;

int reconfigCommon(Stream* streamHandle, void* pluginPayload)
{
    int status = 0;
    uint32_t miid = 0;
    bool is_out_dev = false;
    struct pal_stream_attributes sAttr = {};
    pal_stream_type_t streamType;
    struct sessionToPayloadParam streamData = {};
    struct pal_device dAttr = {};
    Session* sess = nullptr;
    PayloadBuilder* builder = new PayloadBuilder();
    std::shared_ptr<ResourceManager> rmHandle = ResourceManager::getInstance();
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    struct ReconfigPluginPayload* reconfigPld = nullptr;
    struct mixer* mixerHandle = nullptr;
    std::shared_ptr<group_dev_config_t> groupDevConfig = nullptr;
    std::vector<std::pair<int32_t, std::string>> aifBackEndsToConnect;
    std::vector<int> pcmDevIds;
    std::vector<int> frontEndIds;

    PAL_DBG(LOG_TAG,"Enter");
    status = rmHandle->getVirtualAudioMixer(&mixerHandle);
    if (status) {
        PAL_ERR(LOG_TAG, "get mixer handle failed %d", status);
        goto exit;
    }
    status = streamHandle->getStreamAttributes(&sAttr);
    if (status) {
        PAL_ERR(LOG_TAG, "could not get stream attributes\n");
        goto exit;
    }
    status = streamHandle->getAssociatedSession(&sess);
    if (status) {
        PAL_ERR(LOG_TAG, "get mixer handle failed %d", status);
        goto exit;
    }
    reconfigPld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);
    if (!reconfigPld) {
        PAL_ERR(LOG_TAG, "could not get reconfigPld\n");
        goto exit;
    }
    streamType = sAttr.type;
    aifBackEndsToConnect = reconfigPld->aifBackEnds;
    dAttr = reconfigPld->dAttr;
    pcmDevIds = reconfigPld->pcmDevIds;

    if (dAttr.id > PAL_DEVICE_OUT_MIN && dAttr.id < PAL_DEVICE_OUT_MAX)
        is_out_dev = true;

    if (streamType == PAL_STREAM_ULTRASOUND || streamType == PAL_STREAM_LOOPBACK)
    {
        groupDevConfig = rmHandle->getActiveGroupDevConfig();
        if ((((dAttr.id == PAL_DEVICE_OUT_SPEAKER ||
              dAttr.id == PAL_DEVICE_OUT_HANDSET) || (groupDevConfig &&
              dAttr.id == PAL_DEVICE_OUT_ULTRASOUND)) &&
              (streamType == PAL_STREAM_ULTRASOUND)) ||
              (is_out_dev && streamType == PAL_STREAM_LOOPBACK))
        {
            PAL_DBG(LOG_TAG, "PAL_STREAM_ULTRASOUND or PAL_STREAM_LOOPBACK case.");
            if (sess) {
                status = configureMFC(rmHandle,sAttr, dAttr, pcmDevIds,
                                    aifBackEndsToConnect[0].second.data(), builder);
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "build MFC payload failed");
                    goto exit;
                }
            } else {
                PAL_ERR(LOG_TAG, "invalid session ultrasound object");
                status = -EINVAL;
                goto exit;
            }
        }
    } else {
        // /*Setup inCall MFC configuration before the speaker device subgraph moves to START state*/
        // /*Make sure the speaker protection module can apply the correct media format*/
        if (SessionAlsaUtils::isRxDevice(aifBackEndsToConnect[0].first))
            reconfigureInCallMusicStream(dAttr.config, builder);

        /* Configure MFC to match to device config */
        /* This has to be done after sending all mixer controls and before connect */
        if (PAL_STREAM_VOICE_CALL != sAttr.type) {
            if (sAttr.direction == PAL_AUDIO_OUTPUT) {
                if (sess) {
                    status = configureMFC(rmHandle, sAttr, dAttr, pcmDevIds,
                                        aifBackEndsToConnect[0].second.data(), builder);
                    if (status != 0) {
                        PAL_ERR(LOG_TAG, "build MFC payload failed");
                        goto exit;
                    }
                    if (strcmp(dAttr.custom_config.custom_key, "mspp") &&
                        dAttr.id == PAL_DEVICE_OUT_SPEAKER &&
                        dAttr.config.ch_info.channels == 2 &&
                        ((sAttr.type == PAL_STREAM_LOW_LATENCY) ||
                        (sAttr.type == PAL_STREAM_ULTRA_LOW_LATENCY) ||
                        (sAttr.type == PAL_STREAM_PCM_OFFLOAD) ||
                        (sAttr.type == PAL_STREAM_DEEP_BUFFER) ||
                        (sAttr.type == PAL_STREAM_COMPRESSED))) {
                        pal_param_device_rotation_t rotation;
                        status = sess->getFrontEndIds(frontEndIds, RX_HOSTLESS/*not used*/);
                        if (status) {
                            PAL_ERR(LOG_TAG, "getFrontEndId() failed %d", status);
                            goto exit;
                        }
                        status = handleDeviceRotation(rmHandle, streamHandle, rotation.rotation_type,
                                     frontEndIds.at(0), mixerHandle, builder, aifBackEndsToConnect);
                        if (status != 0) {
                            PAL_ERR(LOG_TAG,"build DeviceRotation payload failed");
                            status = 0; //rotaton setting failed is not fatal.
                        }
                    }
                } else {
                    PAL_ERR(LOG_TAG, "invalid session audio object");
                    status = -EINVAL;
                    goto exit;
                }
            }
            if (sAttr.direction == PAL_AUDIO_INPUT) {
                if (strstr(dAttr.custom_config.custom_key , "unprocessed-hdr-mic") &&
                    (dAttr.id == PAL_DEVICE_IN_HANDSET_MIC || dAttr.id == PAL_DEVICE_IN_SPEAKER_MIC)) {
                    status = sess->setParameters(streamHandle, PAL_PARAM_ID_ORIENTATION, nullptr);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "setting HDR record orientation config failed with status %d", status);
                        goto exit;
                    }
                }
                if (sAttr.type == PAL_STREAM_ULTRA_LOW_LATENCY ||
                (dAttr.id == PAL_DEVICE_IN_PROXY || dAttr.id == PAL_DEVICE_IN_RECORD_PROXY)) {
                    if (sess) {
                        status = configureMFC(rmHandle, sAttr, dAttr, pcmDevIds,
                                        aifBackEndsToConnect[0].second.data(), builder);
                        if (status != 0) {
                            PAL_ERR(LOG_TAG, "build MFC payload failed");
                            goto exit;
                        }
                    } else {
                        PAL_ERR(LOG_TAG, "invalid session audio object");
                        status = -EINVAL;
                        goto exit;
                    }
                }
            }
        }
    }

    builder->getCustomPayload(&payload, &payloadSize);
    if (payload) {
        status = SessionAlsaUtils::setMixerParameter(mixerHandle, pcmDevIds.at(0),
                                payload, payloadSize);
    }
    builder->freeCustomPayload();
    payload = NULL;
    payloadSize = 0;
    if (status != 0) {
        PAL_ERR(LOG_TAG, "setMixerParameter failed");
        goto exit;
    }

exit:
    if (builder) {
       delete builder;
       builder = nullptr;
    }
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

/* This is to set devicePP MFC(if exists) and PSPD MFC and stream MFC*/
int configureMFC(const std::shared_ptr<ResourceManager>& rm, struct pal_stream_attributes &sAttr,
            struct pal_device &dAttr, const std::vector<int> &pcmDevIds, const char* intf,
            PayloadBuilder* builder)
{
    int status = 0;
    std::shared_ptr<Device> dev = nullptr;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    struct pal_media_config codecConfig;
    struct sessionToPayloadParam mfcData;
    uint32_t miid = 0;
    bool devicePPMFCSet =  true;
    struct mixer *mixer = nullptr;
    rm->getVirtualAudioMixer(&mixer);
    std::shared_ptr<group_dev_config_t> groupDevConfig;

    PAL_DBG(LOG_TAG,"Enter");

    /* Prepare devicePP MFC payload */
    /* Try to set devicePP MFC for virtual port enabled device to match to DMA config */
    groupDevConfig = rm->getActiveGroupDevConfig();
    if (groupDevConfig && (dAttr.id == PAL_DEVICE_OUT_SPEAKER ||
             dAttr.id == PAL_DEVICE_OUT_HANDSET ||
             dAttr.id == PAL_DEVICE_OUT_ULTRASOUND)) {
        status = SessionAlsaUtils::getModuleInstanceId(mixer, pcmDevIds.at(0), intf,
                                                       TAG_DEVICE_PP_MFC, &miid);
        if (status == 0) {
            PAL_DBG(LOG_TAG, "miid : %x id = %d, data %s, dev id = %d\n", miid,
                    pcmDevIds.at(0), intf, dAttr.id);

            if (groupDevConfig->devpp_mfc_cfg.bit_width)
                mfcData.bitWidth = groupDevConfig->devpp_mfc_cfg.bit_width;
            else
                mfcData.bitWidth = dAttr.config.bit_width;
            if (groupDevConfig->devpp_mfc_cfg.sample_rate)
                mfcData.sampleRate = groupDevConfig->devpp_mfc_cfg.sample_rate;
            else
                mfcData.sampleRate = dAttr.config.sample_rate;
            if (groupDevConfig->devpp_mfc_cfg.channels)
                mfcData.numChannel = groupDevConfig->devpp_mfc_cfg.channels;
            else
                mfcData.numChannel = dAttr.config.ch_info.channels;
            mfcData.ch_info = nullptr;

            builder->payloadMFCConfig((uint8_t**)&payload, &payloadSize, miid, &mfcData);
            if (!payloadSize) {
                PAL_ERR(LOG_TAG, "payloadMFCConfig failed\n");
                status = -EINVAL;
                goto exit;
            }
            status = builder->updateCustomPayload(payload, payloadSize);
            builder->freeCustomPayload(&payload, &payloadSize);
            if (0 != status) {
                PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
                goto exit;
            }
        } else {
            PAL_INFO(LOG_TAG, "deviePP MFC doesn't exist for stream %d \n", sAttr.type);
            devicePPMFCSet = false;
        }

        /* set TKV for slot mask */
        setSlotMask(rm, sAttr, dAttr, pcmDevIds);
    } else if (rm->IsDeviceMuxConfigEnabled() && (dAttr.id == PAL_DEVICE_OUT_SPEAKER ||
              dAttr.id == PAL_DEVICE_OUT_HANDSET)) {
        setSlotMask(rm, sAttr, dAttr, pcmDevIds);
    }

    /* Prepare stream MFC payload */
    if (sAttr.direction == PAL_AUDIO_INPUT) {
        status = SessionAlsaUtils::getModuleInstanceId(mixer, pcmDevIds.at(0), intf,
                                                       TAG_STREAM_MFC_SR, &miid);
        if (status == 0) {
            PAL_DBG(LOG_TAG, "miid : %x id = %d, data %s, dev id = %d\n", miid,
                    pcmDevIds.at(0), intf, dAttr.id);
            if (isPalPCMFormat(sAttr.in_media_config.aud_fmt_id))
                mfcData.bitWidth = ResourceManager::palFormatToBitwidthLookup(
                                                    sAttr.in_media_config.aud_fmt_id);
            else
                mfcData.bitWidth = sAttr.in_media_config.bit_width;
            mfcData.sampleRate = sAttr.in_media_config.sample_rate;
            mfcData.numChannel = sAttr.in_media_config.ch_info.channels;
            mfcData.ch_info = nullptr;
            builder->payloadMFCConfig((uint8_t **)&payload, &payloadSize, miid, &mfcData);
            if (payloadSize && payload) {
                status = builder->updateCustomPayload(payload, payloadSize);
                builder->freeCustomPayload(&payload, &payloadSize);
                if (0 != status) {
                    PAL_ERR(LOG_TAG, "updateCustomPayload failed\n");
                    goto exit;
                }
            }
        }
    }
    /* Prepare PSPD MFC payload */
    /* Get PSPD MFC MIID and configure to match to device config */
    /* This has to be done after sending all mixer controls and before connect */
    status = SessionAlsaUtils::getModuleInstanceId(mixer, pcmDevIds.at(0), intf,
                                                   TAG_DEVICE_MFC_SR, &miid);
    if (status == 0) {
        PAL_DBG(LOG_TAG, "miid : %x id = %d, data %s, dev id = %d\n", miid,
                pcmDevIds.at(0), intf, dAttr.id);

        if (dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_A2DP ||
            dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_SCO ||
            dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_BLE ||
            dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_BLE_BROADCAST) {
            dev = Device::getInstance((struct pal_device *)&dAttr , rm);
            if (!dev) {
                PAL_ERR(LOG_TAG, "Device getInstance failed");
                status = -EINVAL;
                goto exit;
            }
            status = dev->getCodecConfig(&codecConfig);
            if(0 != status) {
                PAL_ERR(LOG_TAG, "getCodecConfig Failed \n");
                goto exit;
            }
            mfcData.bitWidth = codecConfig.bit_width;
            mfcData.sampleRate = codecConfig.sample_rate;
            mfcData.numChannel = codecConfig.ch_info.channels;
            mfcData.ch_info = nullptr;
        } else {
            mfcData.bitWidth = dAttr.config.bit_width;
            if (!devicePPMFCSet && groupDevConfig->devpp_mfc_cfg.sample_rate)
                mfcData.sampleRate = groupDevConfig->devpp_mfc_cfg.sample_rate;
            else
                mfcData.sampleRate = dAttr.config.sample_rate;
            if (!devicePPMFCSet && groupDevConfig->devpp_mfc_cfg.channels)
                mfcData.numChannel = groupDevConfig->devpp_mfc_cfg.channels;
            else
                mfcData.numChannel = dAttr.config.ch_info.channels;
            mfcData.ch_info = nullptr;
        }

        if (dAttr.id == PAL_DEVICE_OUT_AUX_DIGITAL ||
            dAttr.id == PAL_DEVICE_OUT_AUX_DIGITAL_1 ||
            dAttr.id == PAL_DEVICE_OUT_HDMI)
            mfcData.ch_info = &dAttr.config.ch_info;

        builder->payloadMFCConfig((uint8_t **)&payload, &payloadSize, miid, &mfcData);
        if (!payloadSize) {
            PAL_ERR(LOG_TAG, "payloadMFCConfig failed\n");
            status = -EINVAL;
            goto exit;
        }

        status = builder->updateCustomPayload(payload, payloadSize);
        builder->freeCustomPayload(&payload, &payloadSize);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
            goto exit;
        }
    } else {
        PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
        if ((sAttr.direction == (PAL_AUDIO_INPUT | PAL_AUDIO_OUTPUT))||
            (sAttr.type == PAL_STREAM_SENSOR_PCM_RENDERER))
            status = 0;
    }

exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int setSlotMask(const std::shared_ptr<ResourceManager>& rm, struct pal_stream_attributes &sAttr,
            struct pal_device &dAttr, const std::vector<int> &pcmDevIds)
{
    int status = 0;
    std::vector <std::pair<int, int>> tkv;
    struct agm_tag_config* tagConfig = nullptr;
    const char *setParamTagControl = " setParamTag";
    const char *streamPcm = "PCM";
    const char *streamComp = "COMPRESS";
    const char *streamVoice = "VOICEMMODE";
    const char *feCtl = " control";
    struct mixer_ctl *ctl;
    std::ostringstream tagCntrlName;
    std::ostringstream feName;
    std::string backendname;
    int tkv_size = 0;
    uint32_t slot_mask = 0;
    struct mixer *mixer = nullptr;
    std::shared_ptr<group_dev_config_t> groupDevConfig = rm->getActiveGroupDevConfig();

    PAL_DBG(LOG_TAG,"Enter");
    rm->getVirtualAudioMixer(&mixer);

    if (groupDevConfig) {
        if (0 == groupDevConfig->grp_dev_hwep_cfg.slot_mask) {
            slot_mask = slotMaskLUT.at(dAttr.config.ch_info.channels) |
                         slotMaskBwLUT.at(dAttr.config.bit_width);
            tkv.push_back(std::make_pair(TAG_KEY_SLOT_MASK, slot_mask));
        } else {
            tkv.push_back(std::make_pair(TAG_KEY_SLOT_MASK,
                               groupDevConfig->grp_dev_hwep_cfg.slot_mask));
        }
    } else if (rm->IsDeviceMuxConfigEnabled()) {
         slot_mask = slotMaskLUT.at(dAttr.config.ch_info.channels) |
                         slotMaskBwLUT.at(dAttr.config.bit_width);
         tkv.push_back(std::make_pair(TAG_KEY_SLOT_MASK, slot_mask));
    }

    tagConfig = (struct agm_tag_config*)malloc(sizeof(struct agm_tag_config) +
                    (tkv.size() * sizeof(agm_key_value)));

    if (!tagConfig) {
        status = -EINVAL;
        goto exit;
    }

    status = SessionAlsaUtils::getTagMetadata(TAG_DEVICE_MUX, tkv, tagConfig);
    if (0 != status) {
        goto exit;
    }

    if (PAL_STREAM_COMPRESSED == sAttr.type) {
        tagCntrlName<<streamComp<<pcmDevIds.at(0)<<setParamTagControl;
        feName<<streamComp<<pcmDevIds.at(0)<<feCtl;
    } else if (PAL_STREAM_VOICE_CALL == sAttr.type) {
        if (sAttr.info.voice_call_info.VSID == VOICEMMODE1 ||
            sAttr.info.voice_call_info.VSID == VOICELBMMODE1){
            tagCntrlName<<streamVoice<<1<<"p"<<setParamTagControl;
            feName<<streamVoice<<1<<"p"<<feCtl;
        } else {
            tagCntrlName<<streamVoice<<2<<"p"<<setParamTagControl;
            feName<<streamVoice<<2<<"p"<<feCtl;
        }
    } else {
        tagCntrlName<<streamPcm<<pcmDevIds.at(0)<<setParamTagControl;
        feName << streamPcm<<pcmDevIds.at(0)<<feCtl;
    }

    // set FE ctl to BE first in case this is called from connectionSessionDevice
    rm->getBackendName(dAttr.id, backendname);
    ctl = mixer_get_ctl_by_name(mixer, feName.str().data());
    if (!ctl) {
        PAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", feName.str().data());
        status = -EINVAL;
        goto exit;
    }
    mixer_ctl_set_enum_by_string(ctl, backendname.c_str());
    ctl = nullptr;

    // set tag data
    ctl = mixer_get_ctl_by_name(mixer, tagCntrlName.str().data());
    if (!ctl) {
        PAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", tagCntrlName.str().data());
        status = -EINVAL;
        goto exit;
    }
    tkv_size = tkv.size()*sizeof(struct agm_key_value);
    status = mixer_ctl_set_array(ctl, tagConfig, sizeof(struct agm_tag_config) + tkv_size);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "failed to set the tag calibration %d", status);
    }

exit:
    if (tagConfig)
        free(tagConfig);
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int reconfigureModule(SessionAlsaPcm* session, PayloadBuilder* builder, uint32_t tagID, const char* BE, struct sessionToPayloadParam *data)
{
    uint32_t status = 0;
    uint32_t miid = 0;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    std::vector<int> pcmDevIds;
    struct mixer* mxr;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

    PAL_DBG(LOG_TAG, "Enter");
    status = session->getMIID(BE, tagID, &miid);
    if(status){
        PAL_INFO(LOG_TAG,"could not find tagID 0x%x for backend %s", tagID, BE);
        goto exit;
    }
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    status = session->getFrontEndIds(pcmDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevIds) failed %d", status);
        goto exit;
    }
    PAL_DBG(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
    builder->payloadMFCConfig(&payload, &payloadSize, miid, data);
    if (payloadSize && payload) {
        status = builder->updateCustomPayload(payload, payloadSize);
        builder->freeCustomPayload(&payload, &payloadSize);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
            status = -EINVAL;
            goto exit;
        }
    }
    builder->getCustomPayload(&payload, &payloadSize);
    status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                payload, payloadSize);
    builder->freeCustomPayload();
    if (status) {
        PAL_ERR(LOG_TAG, "setMixerParameter failed");
        goto exit;
    }

exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int32_t reconfigureInCallMusicStream(struct pal_media_config config, PayloadBuilder* builder)
{
    int status = 0;
    std::list<Stream*>::iterator it;
    Session* sess = nullptr;
    SessionAlsaPcm* session = nullptr;
    struct pal_stream_attributes sAttr;
    std::list<Stream*> activeStreams;
    pal_stream_direction_t dir;
    struct sessionToPayloadParam deviceData;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

    PAL_DBG(LOG_TAG, "Enter");
    status = rm->getActiveStreamByType_l(activeStreams, PAL_STREAM_VOICE_CALL_MUSIC);
    if(status){
        PAL_ERR(LOG_TAG, "failed to get active streams for stream type PAL_STREAM_VOICE_CALL_MUSIC");
        goto exit;
    }
    if (!activeStreams.size()) {
        PAL_DBG(LOG_TAG, "No In-Call Music Stream found to configure");
        goto exit;
    }
    for (auto& str: activeStreams) {
        str->getStreamAttributes(&sAttr);
        status = str->getStreamAttributes(&sAttr);
        if (status != 0) {
            PAL_ERR(LOG_TAG, "stream get attributes failed");
            goto exit;
        }
        if (sAttr.info.incall_music_info.local_playback) {
            PAL_INFO(LOG_TAG, "found incall stream to configure");
            status = str->getAssociatedSession(&sess);
            if (!sess) {
                PAL_ERR(LOG_TAG, "No associated session for stream exist");
                status = -EINVAL;
                goto exit;
            }
            deviceData.bitWidth = config.bit_width;
            deviceData.sampleRate = config.sample_rate;
            deviceData.numChannel = config.ch_info.channels;
            deviceData.ch_info = nullptr;
            status = reconfigureModule(session, builder, PER_STREAM_PER_DEVICE_MFC, "ZERO", &deviceData);
        }
    }
exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int handleDeviceRotation(const std::shared_ptr<ResourceManager>& rm, Stream *s,
                    pal_speaker_rotation_type rotation_type, int device, struct mixer *mixer,
                    PayloadBuilder* builder, std::vector<std::pair<int32_t, std::string>> rxAifBackEnds)
{
    int status = 0;
    struct pal_stream_attributes sAttr = {};
    struct pal_device dAttr = {};
    uint32_t miid = 0;
    uint8_t* alsaParamData = nullptr;
    size_t alsaPayloadSize = 0;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    int mfc_tag = TAG_MFC_SPEAKER_SWAP;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::shared_ptr<group_dev_config_t> groupDevConfig;

    PAL_DBG(LOG_TAG,"Enter");
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "stream get attributes failed");
        return status;
    }

    if (PAL_AUDIO_OUTPUT== sAttr.direction) {
        status = s->getAssociatedDevices(associatedDevices);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "getAssociatedDevices Failed\n");
            return status;
        }

        for (int i = 0; i < associatedDevices.size(); i++) {
             status = associatedDevices[i]->getDeviceAttributes(&dAttr);
             if (0 != status) {
                 PAL_ERR(LOG_TAG, "get Device Attributes Failed\n");
                 return status;
             }

             if ((PAL_DEVICE_OUT_SPEAKER == dAttr.id) &&
                  (2 == dAttr.config.ch_info.channels) &&
                  (strcmp(dAttr.custom_config.custom_key, "mspp") != 0)) {
                 /* Get DevicePP MFC MIID and configure to match to device config */
                 /* This has to be done after sending all mixer controls and
                  * before connect
                  */
                status =
                        SessionAlsaUtils::getModuleInstanceId(mixer,
                                                              device,
                                                              rxAifBackEnds[i].second.data(),
                                                              mfc_tag, &miid);
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
                    return status;
                }
                PAL_DBG(LOG_TAG, "miid : %x id = %d, data %s, dev id = %d\n", miid,
                    device, rxAifBackEnds[i].second.data(), dAttr.id);

                groupDevConfig = rm->getActiveGroupDevConfig();
                if (groupDevConfig) {
                    if (groupDevConfig->devpp_mfc_cfg.channels)
                        dAttr.config.ch_info.channels = groupDevConfig->devpp_mfc_cfg.channels;
                }
                builder->payloadMFCMixerCoeff((uint8_t **)&alsaParamData,
                                            &alsaPayloadSize, miid,
                                            dAttr.config.ch_info.channels,
                                            rotation_type);

                if (alsaPayloadSize) {
                    status = builder->updateCustomPayload(alsaParamData, alsaPayloadSize);
                    builder->freeCustomPayload(&alsaParamData, &alsaPayloadSize);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
                        return status;
                    }
                }
                builder->getCustomPayload(&payload, &payloadSize);
                status = SessionAlsaUtils::setMixerParameter(mixer,
                                                             device,
                                                             payload,
                                                             payloadSize);
                builder->freeCustomPayload();
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "setMixerParameter failed");
                    return status;
                }
            }
        }
    }
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int32_t pluginConfigSetParam(Stream* s, void* pluginPayload)
{
    int status = 0;
    int setConfigStatus = 0;
    uint32_t paramId = -1;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();
    struct mixer* mxr = nullptr;
    PayloadBuilder* builder = nullptr;
    std::vector<int> frontEndIds;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    SetParamPluginPayload* ppld = reinterpret_cast<SetParamPluginPayload*>(pluginPayload);
    pal_stream_attributes sAttr = {};
    pal_device dAttr = {};
    SessionAR* session = dynamic_cast<SessionAR*>(ppld->session);
    paramId = ppld->paramId;

    PAL_DBG(LOG_TAG, "Enter, paramId: %d", paramId);
    switch (paramId) {
        case PAL_PARAM_ID_DEVICE_ROTATION:
        {
            bool doDevPPMute = false;
            status = s->getStreamAttributes(&sAttr);
            if (status) {
                PAL_ERR(LOG_TAG, "could not get stream attributes\n");
                goto exit;
            }
            status = session->getFrontEndIds(frontEndIds);
            if (status) {
                PAL_ERR(LOG_TAG, "getFrontEndId() failed %d", status);
                goto exit;
            }
            status = rm->getVirtualAudioMixer(&mxr);
            if (status) {
                PAL_ERR(LOG_TAG, "get mixer handle failed %d", status);
                goto exit;
            }
            rxAifBackEnds = session->getRxBEVecRef();
            builder = reinterpret_cast<PayloadBuilder*>(ppld->builder);
            /* To avoid pop while switching channels, it is required to mute
               the playback first and then swap the channel and unmute */
            if (sAttr.type == PAL_STREAM_LOW_LATENCY ||
                    sAttr.type == PAL_STREAM_ULTRA_LOW_LATENCY) {
                    setConfigStatus = session->setConfig(s, MODULE, MUTE_TAG);
            } else if (PAL_AUDIO_OUTPUT == sAttr.direction) {
                /* Need to check if there is a valid module available
                 * for DEVICEPP_MUTE to avoid false negative failing
                 * setConfig message.*/
                status = s->getAssociatedDevices(associatedDevices);
                if (0 != status) {
                    PAL_ERR(LOG_TAG, "getAssociatedDevices Failed\n");
                    goto exit;
                }
                for (int i = 0; i < associatedDevices.size(); i++) {
                    status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "getDeviceAttributes Failed\n");
                        break;
                    }
                    if ((PAL_DEVICE_OUT_SPEAKER == dAttr.id) &&
                        (2 == dAttr.config.ch_info.channels) &&
                        (strcmp(dAttr.custom_config.custom_key, "mspp") != 0)) {
                        doDevPPMute = true;
                        break;
                    }
                }
                if (doDevPPMute) {
                    setConfigStatus = session->setConfig(s, MODULE, DEVICEPP_MUTE);
                }
            }
            if (setConfigStatus) {
                PAL_INFO(LOG_TAG, "DevicePP Mute failed");
            }
            //mStreamMutex.unlock(); NEED TO FIGURE OUT A WAY TO UNLOCK DURING SLEEP
            usleep(MUTE_RAMP_PERIOD); // Wait for Mute ramp down to happen
            // mStreamMutex.lock();
            pal_param_device_rotation_t *rotation =
                                     reinterpret_cast<pal_param_device_rotation_t *>(ppld->payload);
            status = handleDeviceRotation(rm, s, rotation->rotation_type, frontEndIds.at(0), mxr,
                                          builder, rxAifBackEnds);
            // mStreamMutex.unlock();
            usleep(MUTE_RAMP_PERIOD); // Wait for channel swap to take affect
            // mStreamMutex.lock();
            if (sAttr.type == PAL_STREAM_LOW_LATENCY ||
                    sAttr.type == PAL_STREAM_ULTRA_LOW_LATENCY) {
                    setConfigStatus = session->setConfig(s, MODULE, UNMUTE_TAG);
            } else if (doDevPPMute) {
                setConfigStatus = session->setConfig(s, MODULE, DEVICEPP_UNMUTE);
            }
            if (setConfigStatus) {
                PAL_INFO(LOG_TAG, "DevicePP Unmute failed");
            }
            break;
        }
        break;
        default:
            PAL_ERR(LOG_TAG, "paramId %d, is unsupported", paramId);
            status = -EINVAL;
    }
exit:
    PAL_DBG(LOG_TAG,"Exit status: %d", status);
    return status;
}

/*
 *
 * 1. Register to listen at AGM level for Silence Detection Even
 * 2. Register a Callback to receive events from DSP
 * 3. Get MIID of HW_ENDPOINT_TX
 * 4. Configure PARAM_ID_SILECENCE_DETECTION payload
 *
 **/
int enableSilenceDetection(const std::shared_ptr<ResourceManager> rm,
                struct mixer *mixer, const std::vector<int> &devIds,
                const char *intf_name, uint64_t cookie)
{
    int status = 0;
    uint32_t miid = 0;
    size_t pad_bytes = 0, payloadSize = 0;
    uint8_t* payload = NULL;
    struct apm_module_param_data_t* header = NULL;
    param_id_silence_detection_t *silence_detection_cfg = NULL;
    struct agm_event_reg_cfg event_cfg = {};

    if (silenceEventRegistered == true)
        goto silence_ev_setup_done;


    PAL_INFO(LOG_TAG, "Registering For Silence Detection Events \n");
    event_cfg.event_id = EVENT_ID_SILENCE_DETECTION;
    event_cfg.event_config_payload_size = 0;
    event_cfg.is_register = 1;

    status  = SessionAlsaUtils::registerMixerEvent(mixer, devIds.at(0), intf_name,
                    DEVICE_HW_ENDPOINT_TX, (void *)&event_cfg,
                    sizeof(struct agm_event_reg_cfg));
    if (status) {
        PAL_ERR(LOG_TAG, "Failed Registering for SILENCE DETECTION EVENT\n");
        goto silence_ev_setup_done;
    }

    PAL_INFO(LOG_TAG, "Registered for Silence Detection Event\n");

    status = rm->registerMixerEventCallback(devIds, handleSilenceDetectionCb, cookie, true);
    if (status != 0) {
          PAL_ERR(LOG_TAG, "Failed to register DSP cb for silence detection Event");
          goto err_silence_ev_cb_reg;
    }
    PAL_INFO(LOG_TAG, "Registered CB for Silence Detection\n");
    silenceEventRegistered = true;
    PAL_INFO(LOG_TAG, "Silence Detection setup successfully \n");

    goto silence_ev_setup_done;

err_silence_ev_setup:
                rm->registerMixerEventCallback(devIds, handleSilenceDetectionCb,
                                cookie, false);
err_silence_ev_cb_reg:
                event_cfg.is_register = 0;
                status  = SessionAlsaUtils::registerMixerEvent(mixer, devIds.at(0),
                               intf_name, DEVICE_HW_ENDPOINT_TX,
                                (void *)&event_cfg, sizeof(struct agm_event_reg_cfg));
silence_ev_setup_done:
                status = 0;
    return status;
}


int disableSilenceDetection(const std::shared_ptr<ResourceManager> rm,
                struct mixer *mixer, const std::vector<int> &devIds,
                const char *intf_name, uint64_t cookie)
{
    int status = 0;
    struct agm_event_reg_cfg event_cfg = {};

    event_cfg.event_id = EVENT_ID_SILENCE_DETECTION;
    event_cfg.event_config_payload_size = 0;
    event_cfg.is_register = 0;

    if (silenceEventRegistered == false)
        goto exit;

    PAL_INFO(LOG_TAG, "De-registering For Silence Detection Events\n");
    status  = SessionAlsaUtils::registerMixerEvent(mixer, devIds.at(0),
                 intf_name, DEVICE_HW_ENDPOINT_TX,
                 (void *)&event_cfg, sizeof(struct agm_event_reg_cfg));
    if (status)
        PAL_ERR(LOG_TAG, "Unable to deregister SILENCE DETECTION EVENT\n");

    status = rm->registerMixerEventCallback(devIds, handleSilenceDetectionCb, cookie, false);
    if (status != 0)
        PAL_ERR(LOG_TAG, "Failed to deregister  silence detection Callback to rm");

    silenceEventRegistered = false;

exit:
    return status;
}

int dump_kernel_log(char *kmsg_out_file)
{
    int kmsg_fd = 0, log_out_size = 0;
    ssize_t kernel_buf_size = 0;
    char *kernel_buf = NULL;

    kernel_buf_size = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);
    PAL_INFO(LOG_TAG, "%s::kernel_buf_size :: %zd", __func__, kernel_buf_size);

    kernel_buf = (char *)malloc(kernel_buf_size);
    if (!kernel_buf) {
        PAL_ERR(LOG_TAG, "%s:: error allocating memory", __func__);
        return -ENOMEM;
    }

    kmsg_fd = open(kmsg_out_file, O_CREAT|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
    if (kmsg_fd < 0){
        PAL_ERR(LOG_TAG, "%s::Error opening kernel msg out file", __func__);
        free(kernel_buf);
        return kmsg_fd;
    }

    klogctl(SYSLOG_ACTION_READ_ALL, kernel_buf, kernel_buf_size);
    log_out_size = write(kmsg_fd, kernel_buf, kernel_buf_size);
    if (log_out_size < 0) {
       PAL_ERR(LOG_TAG, "%s: %s  Unable to write.\n", __func__, kmsg_out_file);
       goto close_kmsg_fd;
    }
    PAL_INFO(LOG_TAG, "%s: Writing %s log, %d bytes\n",__func__,
                    kmsg_out_file, log_out_size);

close_kmsg_fd:
    close(kmsg_fd);
    free(kernel_buf);
    return log_out_size;
}

int dump_registers(char *in_file_path, char *regdump_out_file)
{
    char *reg_dump = NULL;
    int infile_fd = 0,  regdump_wr_fd = 0;
    int read_out_size = 0, regdump_size = 0;
    size_t sysfs_page_size = sysconf(_SC_PAGESIZE);

    reg_dump = (char *)malloc(REGDUMP_OUT_SIZE);
    if (!reg_dump) {
        PAL_ERR(LOG_TAG, "%s:: error allocating memory", __func__);
        return -ENOMEM;
    }

    infile_fd = open(in_file_path, O_RDONLY);
    if (infile_fd < 0) {
       PAL_ERR(LOG_TAG, "%s: %s  not found.\n", __func__, in_file_path);
       read_out_size = -1;
       goto free_buf;
    }

    PAL_INFO(LOG_TAG, "Reading %s regdump interface \n", in_file_path);
    read_out_size = read(infile_fd, reg_dump, REGDUMP_OUT_SIZE);
    if (read_out_size < 0) {
       PAL_ERR(LOG_TAG, "%s: %s  Unable to Read.\n", __func__, in_file_path);
       read_out_size  = -1;
       goto close_infile;
    }
    PAL_INFO(LOG_TAG, "Regdump ReadOut Buffer Size = %d", read_out_size);

    regdump_wr_fd = open(regdump_out_file, O_CREAT|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
    if (regdump_wr_fd < 0) {
       PAL_ERR(LOG_TAG, "%s: %s  Unable to Open for writing.\n", __func__, regdump_out_file);
       read_out_size = -1;
       goto close_infile;
    }
    regdump_size =  write(regdump_wr_fd, reg_dump, read_out_size);
    if (regdump_size < 0) {
       PAL_ERR(LOG_TAG, "%s: %s  Unable to write.\n", __func__, regdump_out_file);
       read_out_size = -1;
       goto close_regdump_file;
    }
    PAL_INFO(LOG_TAG, "Bolero Regmap Dump Size %ld and file %s", regdump_size, regdump_out_file);

close_regdump_file:
    close(regdump_wr_fd);
close_infile:
    close(infile_fd);
free_buf:
    free(reg_dump);
    return read_out_size;
}


int dump_silence_event_status(char *out_file, uint32_t channel_group, uint32_t status_ch_mask)
{
    char event_data_buf[255];
    int pos = 0,  write_out_bytes = 0, fd = 0;

    pos = snprintf(event_data_buf, 255, "channel_group :: %u \n",
                        channel_group);
    pos += snprintf(event_data_buf+pos, 255-pos, "Channel_Status :: %u \n",
                        status_ch_mask);

    fd = open(out_file, O_CREAT|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
    if (fd < 0) {
        PAL_ERR(LOG_TAG,
                "%s::Error Opening silence data status file\n", __func__);
        return -EINVAL;
    }

    write_out_bytes = write(fd, event_data_buf, pos);
    if (write_out_bytes < 1)
       PAL_ERR(LOG_TAG, "%s::failed writing silence event status",__func__);

    close(fd);
    return write_out_bytes;
}

/*
 *Callback from DSP for SILENCE Detection
 */
void handleSilenceDetectionCb(uint64_t hdl __unused, uint32_t event_id, void *event_data, uint32_t event_size)
{
    char out_file_name[MAX_DUMP_FILENAME_SIZE];
    uint32_t channel_group = 0 , status_ch_mask = 0;
    event_cfg_silence_detection_t *silence_event = nullptr;
    struct tm *timenow;
    time_t now = time(NULL);
    timenow = gmtime(&now);
    PAL_INFO(LOG_TAG, "Silence Detection event raised\n");

    switch (event_id) {

    case EVENT_ID_SILENCE_DETECTION:
        PAL_INFO(LOG_TAG, "EVENT_ID_SILENCE_DETECTION received from DSP\n");

        strftime(out_file_name, MAX_DUMP_FILENAME_SIZE,
                        SILENCE_EVENT_INFO, timenow);

        silence_event = (event_cfg_silence_detection_t *)event_data;
        channel_group = silence_event->num_32_channel_group;
        status_ch_mask = silence_event->detections[0].status_ch_mask;
        dump_silence_event_status(out_file_name, channel_group, status_ch_mask);

        /*
         * Read BOLERO/CDC Registers
         **/
        strftime(out_file_name, MAX_DUMP_FILENAME_SIZE,
                        BOLERO_REGDUMP_OUT_PATH, timenow);
        dump_registers(BOLERO_PROC_INTF, out_file_name);

        /*
         * Read SWR VA Macro Registers
         **/
        strftime(out_file_name, MAX_DUMP_FILENAME_SIZE,
                        VA_SWR_REGDUM_OUT_PATH, timenow);
        dump_registers(VA_SWR_PROC_INTF, out_file_name);

        /*
         * Read WCD939X  Registers
         **/
        strftime(out_file_name, MAX_DUMP_FILENAME_SIZE,
                        WCD939X_REGDUMP_OUT_PATH, timenow);
        dump_registers(WCD939X_PROC_INTF, out_file_name);

        /*
         * kernel msg (/dev/kmsg) read
         **/
        strftime(out_file_name, MAX_DUMP_FILENAME_SIZE,
                        KMSG_OUT_FILE, timenow);
        dump_kernel_log(out_file_name);

        break;

    default:

        break;

    }

    return;
}