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
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "PAL: libsession_pcm_config"

#include <log/log.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <agm/agm_api.h>
#include <asps/asps_acm_api.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <amdb_api.h>
#include "audio_dam_buffer_api.h"
#include "apm_api.h"
#include "us_gen_api.h"

#include "Stream.h"
#include "ResourceManager.h"
#include "PalAudioRoute.h"
#include "PluginManagerIntf.h"
#include "SessionAlsaPcm.h"
#include "SessionAlsaUtils.h"
#include "acd_api.h"

//SessionAlsaPcm EVENT_ID_XXXs
#include "detection_cmn_api.h"
#include "sh_mem_pull_push_mode_api.h"
#include "us_detect_api.h"
#include "rx_haptics_api.h"
#include "hw_intf_cmn_api.h"
#include "PayloadBuilder.h"
#include "SessionAR.h"
#include "ConfigSessionAlsaPcm.h"
#include "ConfigSessionUtils.h"

// ASR handlecb def supports
#include "asr_module_calibration_api.h"
#include <fcntl.h>
#include <sys/stat.h>
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

/*interface implementation*/
extern "C" int pcmPluginConfig(Stream* stream, plugin_config_name_t config,
                 void *pluginPayload, size_t ppldSize)
{
    int status = 0;
    PAL_DBG(LOG_TAG, "Enter");
    switch(config) {
        case PAL_PLUGIN_CONFIG_START:
            status = pcmPluginConfigSetConfigStart(stream, pluginPayload);
            break;
        case PAL_PLUGIN_CONFIG_STOP:
            status = pcmPluginConfigSetConfigStop(stream, pluginPayload);
            break;
        case PAL_PLUGIN_RECONFIG:
            status = reconfigCommon(stream, pluginPayload);
            break;
        case PAL_PLUGIN_CONFIG_SETPARAM:
            status = pluginConfigSetParam(stream, pluginPayload);
            break;
        default:
            PAL_ERR(LOG_TAG, "config type %d, is unsupported",config);
            status = -EINVAL;
    }
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int32_t pcmPluginConfigSetConfigStart(Stream* s, void* pluginPayload)
{
    int32_t status = 0;
    std::shared_ptr<ResourceManager> rm;
    pal_stream_attributes sAttr = {};
    pal_device dAttr = {};
    struct sessionToPayloadParam streamData = {};
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    Session* sess = nullptr;
    SessionAlsaPcm* session = nullptr;
    PayloadBuilder* builder = nullptr;
    struct mixer* mxr = nullptr;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    uint32_t miid = 0;
    PluginPayload* ppld = nullptr;
    struct agm_event_reg_cfg event_cfg = {};
    struct pal_media_config codecConfig = {};
    struct agm_event_reg_cfg *acd_event_cfg = nullptr;
    struct agm_event_reg_cfg *asr_event_cfg = nullptr;
    std::vector<int> pcmDevIds;
    int DeviceId = 0;
    int tagId = 0;
    int payload_size = 0;

    PAL_DBG(LOG_TAG, "Enter");
    memset(&streamData, 0, sizeof(struct sessionToPayloadParam));
    memset(&dAttr, 0, sizeof(struct pal_device));
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "stream get attributes failed");
        goto exit;
    }
    ppld = reinterpret_cast<PluginPayload*>(pluginPayload);
    builder = reinterpret_cast<PayloadBuilder*>(ppld->builder);
    sess = ppld->session;
    session = static_cast<SessionAlsaPcm*>(sess);

    if (session == nullptr) {
        PAL_INFO(LOG_TAG, "session is NULL!!!\n");
        goto exit;
    }
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    rxAifBackEnds = session->getRxBEVecRef();
    txAifBackEnds = session->getTxBEVecRef();
    status = session->getFrontEndIds(pcmDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }

    if (sAttr.type == PAL_STREAM_VOICE_UI) {
        uint32_t svaMiid;
        payload_size = sizeof(struct agm_event_reg_cfg);
        memset(&event_cfg, 0, sizeof(event_cfg));
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 1;
        event_cfg.event_id = s->getCallbackEventId();
        svaMiid = session->getsvaMiid();
        event_cfg.module_instance_id = svaMiid;
        SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            (void *)&event_cfg, payload_size);
        memset(&event_cfg, 0, sizeof(event_cfg));
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 1;
        event_cfg.event_id = EVENT_ID_SH_MEM_PUSH_MODE_EOS_MARKER;
        tagId = SHMEM_ENDPOINT;
        SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            txAifBackEnds[0].second.data(), tagId, (void *)&event_cfg,
            payload_size);
    } else if (sAttr.type == PAL_STREAM_ULTRASOUND && session->getRegisterForEvents()) {
        payload_size = sizeof(struct agm_event_reg_cfg);
        std::vector<int> pcmDevTxIds;
        status = session->getFrontEndIds(pcmDevTxIds, TX_HOSTLESS);
        if (status) {
            PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevTxIds) failed %d", status);
            goto exit;
        }
        memset(&event_cfg, 0, sizeof(event_cfg));
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 1;
        event_cfg.event_id = EVENT_ID_GENERIC_US_DETECTION;
        tagId = ULTRASOUND_DETECTION_MODULE;
        DeviceId = pcmDevTxIds.at(0);
        SessionAlsaUtils::registerMixerEvent(mxr, DeviceId,
                txAifBackEnds[0].second.data(), tagId, (void *)&event_cfg,
                payload_size);
    } else if (sAttr.type == PAL_STREAM_ACD) {
        PAL_DBG(LOG_TAG, "register ACD models");
        /*IS THIS doing anything??? where is the customPayloadset???*/
        builder->getCustomPayload(&payload, &payloadSize);
        SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                            payload, payloadSize);
        builder->freeCustomPayload();
    } else if (sAttr.type == PAL_STREAM_CONTEXT_PROXY) {
        status = register_asps_event(1, session, mxr);
    } else if (sAttr.type == PAL_STREAM_HAPTICS &&
            sAttr.info.opt_stream_info.haptics_type == PAL_STREAM_HAPTICS_TOUCH) {
        payload_size = sizeof(struct agm_event_reg_cfg);
        memset(&event_cfg, 0, sizeof(event_cfg));
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 1;
        event_cfg.event_id = EVENT_ID_WAVEFORM_STATE;
        SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                rxAifBackEnds[0].second.data(), MODULE_HAPTICS_GEN, (void *)&event_cfg,
                payload_size);
    } else if (sAttr.type == PAL_STREAM_ASR) {
        void *eventPayload = nullptr;
        size_t eventPayloadSize = 0;
        uint32_t asrMiid = session->getAsrMiid();
        session->getEventPayload(&eventPayload, &eventPayloadSize);
        payload_size = sizeof(struct agm_event_reg_cfg) + eventPayloadSize;
        asr_event_cfg = (struct agm_event_reg_cfg *)calloc(1, payload_size);
        memset(&event_cfg, 0, sizeof(event_cfg));
        asr_event_cfg->event_config_payload_size = eventPayloadSize;
        asr_event_cfg->is_register = 1;
        asr_event_cfg->event_id = session->getEventId();
        asr_event_cfg->module_instance_id = asrMiid;
        memcpy(asr_event_cfg->event_config_payload, eventPayload, eventPayloadSize);
        if (pcmDevIds.size() == 0) {
            PAL_ERR(LOG_TAG, "frontendIDs is not available.");
            status = -EINVAL;
            goto exit;
        }
        SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            (void *)asr_event_cfg, payload_size);
    }

    switch (sAttr.direction) {
        case PAL_AUDIO_INPUT:
            if (pcmDevIds.size() == 0) {
                PAL_ERR(LOG_TAG, "frontendIDs is not available.");
                status = -EINVAL;
                goto exit;
            }
            if ((sAttr.type != PAL_STREAM_VOICE_UI) &&
                (sAttr.type != PAL_STREAM_ACD) &&
                (sAttr.type != PAL_STREAM_ASR) &&
                (sAttr.type != PAL_STREAM_CONTEXT_PROXY) &&
                (sAttr.type != PAL_STREAM_SENSOR_PCM_DATA) &&
                (sAttr.type != PAL_STREAM_COMMON_PROXY)) {
                /* Get MFC MIID and configure to match to stream config */
                /* This has to be done after sending all mixer controls and before connect */
                if (sAttr.type != PAL_STREAM_VOICE_CALL_RECORD)
                    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                                                txAifBackEnds[0].second.data(),
                                                                TAG_STREAM_MFC_SR, &miid);
                else
                    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                                                "ZERO", TAG_STREAM_MFC_SR, &miid);
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
                    goto exit;
                }
                if (sAttr.type != PAL_STREAM_VOICE_CALL_RECORD) {
                    PAL_ERR(LOG_TAG, "miid : %x id = %d, data %s\n", miid,
                        pcmDevIds.at(0), txAifBackEnds[0].second.data());
                } else {
                    PAL_ERR(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
                }

                if (isPalPCMFormat(sAttr.in_media_config.aud_fmt_id))
                    streamData.bitWidth =
                        ResourceManager::palFormatToBitwidthLookup(sAttr.in_media_config.aud_fmt_id);
                else
                    streamData.bitWidth = sAttr.in_media_config.bit_width;
                streamData.sampleRate = sAttr.in_media_config.sample_rate;
                streamData.numChannel = sAttr.in_media_config.ch_info.channels;
                streamData.ch_info = nullptr;
                builder->payloadMFCConfig(&payload, &payloadSize, miid, &streamData);
                if (payloadSize && payload) {
                    status = builder->updateCustomPayload(payload, payloadSize);
                    builder->freeCustomPayload(&payload, &payloadSize);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
                        goto exit;
                    }
                }
                if (sAttr.type == PAL_STREAM_VOIP_TX) {
                    status = SessionAlsaUtils::getModuleInstanceId(mxr,
                                                        pcmDevIds.at(0),
                                            txAifBackEnds[0].second.data(),
                                                TAG_DEVICEPP_EC_MFC, &miid);
                    if (status != 0) {
                        PAL_ERR(LOG_TAG,"getModuleInstanceId failed\n");
                        goto set_mixer;
                    }
                    PAL_INFO(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
                    status = s->getAssociatedDevices(associatedDevices);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
                        goto set_mixer;
                    }
                    for (int i = 0; i < associatedDevices.size();i++) {
                        status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                        if (0 != status) {
                            PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                            goto set_mixer;
                        }
                        if ((dAttr.id == PAL_DEVICE_IN_BLUETOOTH_A2DP) ||
                            (dAttr.id == PAL_DEVICE_IN_BLUETOOTH_BLE) ||
                            (dAttr.id == PAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET)) {
                            struct pal_media_config codecConfig;
                            status = associatedDevices[i]->getCodecConfig(&codecConfig);
                            if (0 != status) {
                                PAL_ERR(LOG_TAG, "getCodecConfig Failed \n");
                                goto set_mixer;
                            }
                            streamData.sampleRate = codecConfig.sample_rate;
                            streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                            streamData.numChannel = 0xFFFF;
                        } else if (dAttr.id == PAL_DEVICE_IN_USB_DEVICE ||
                                dAttr.id == PAL_DEVICE_IN_USB_HEADSET) {
                            streamData.sampleRate = (dAttr.config.sample_rate % SAMPLINGRATE_8K == 0 &&
                                                    dAttr.config.sample_rate <= SAMPLINGRATE_48K) ?
                                                    dAttr.config.sample_rate : SAMPLINGRATE_48K;
                            streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                            streamData.numChannel = 0xFFFF;
                        } else {
                            streamData.sampleRate = dAttr.config.sample_rate;
                            streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                            streamData.numChannel = 0xFFFF;
                        }
                        builder->payloadMFCConfig(&payload, &payloadSize, miid, &streamData);
                        if (payloadSize && payload) {
                            status = builder->updateCustomPayload(payload, payloadSize);
                            builder->freeCustomPayload(&payload, &payloadSize);
                            if (0 != status) {
                                PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                                goto set_mixer;
                            }
                        }
                    }
                }
                if (sAttr.type == PAL_STREAM_VOIP_TX) {
                    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                            txAifBackEnds[0].second.data(), DEVICE_MFC, &miid);
                    if (status != 0) {
                        PAL_ERR(LOG_TAG,"getModuleInstanceId failed\n");
                        goto configure_pspfmfc;
                    }
                    PAL_INFO(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
                    status = s->getAssociatedDevices(associatedDevices);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
                        goto set_mixer;
                    }
                    for (int i = 0; i < associatedDevices.size();i++) {
                        status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                        if (0 != status) {
                            PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                            goto set_mixer;
                        }
                        if (dAttr.id == PAL_DEVICE_IN_USB_DEVICE || dAttr.id == PAL_DEVICE_IN_USB_HEADSET) {
                            streamData.sampleRate = (dAttr.config.sample_rate % SAMPLINGRATE_8K == 0 &&
                                                    dAttr.config.sample_rate <= SAMPLINGRATE_48K) ?
                                                    dAttr.config.sample_rate : SAMPLINGRATE_48K;
                            streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                            streamData.numChannel = 0xFFFF;
                        } else {
                            streamData.sampleRate = dAttr.config.sample_rate;
                            streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                            streamData.numChannel = 0xFFFF;
                        }
                        builder->payloadMFCConfig(&payload, &payloadSize, miid, &streamData);
                        if (payloadSize && payload) {
                            status = builder->updateCustomPayload(payload, payloadSize);
                            builder->freeCustomPayload(&payload, &payloadSize);
                            if (0 != status) {
                                PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                                goto set_mixer;
                            }
                        }
                    }
                }
configure_pspfmfc:
            status = s->getAssociatedDevices(associatedDevices);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
                goto set_mixer;
            }
            if (associatedDevices.size() < 1) {
                PAL_ERR(LOG_TAG,"no device present\n");
                goto set_mixer;
            }
            status = associatedDevices[0]->getDeviceAttributes(&dAttr);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                goto set_mixer;
            }
            if (dAttr.id == PAL_DEVICE_IN_PROXY || dAttr.id == PAL_DEVICE_IN_RECORD_PROXY) {
                status = configureMFC(rm, sAttr, dAttr, pcmDevIds,
                txAifBackEnds[0].second.data(), builder);
                if(status != 0) {
                    PAL_ERR(LOG_TAG, "build MFC payload failed");
                }
            }
set_mixer:
            builder->getCustomPayload(&payload, &payloadSize);
            status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                         payload, payloadSize);
            builder->freeCustomPayload();
            if (status != 0) {
                PAL_ERR(LOG_TAG, "setMixerParameter failed");
                goto exit;
            }
            if (sAttr.type == PAL_STREAM_ULTRA_LOW_LATENCY ||
                sAttr.type == PAL_STREAM_LOW_LATENCY) {
                status = session->setConfig(s, MODULE, PUSHPULL_CHMIXER_COEFFICIENT);
                if (status)
                    PAL_ERR(LOG_TAG, "Failed setConfig status=%d", status);
            }
            if (sAttr.type == PAL_STREAM_VOICE_CALL_RECORD) {
                status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                                            "ZERO", RAT_RENDER, &miid);
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
                    goto exit;
                }
                PAL_INFO(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
                codecConfig.bit_width = sAttr.in_media_config.bit_width;
                codecConfig.sample_rate = 48000;
                codecConfig.aud_fmt_id =  sAttr.in_media_config.aud_fmt_id;
                /* RAT RENDER always set to stereo for uplink+downlink record*/
                /* As mux_demux gives only stereo o/p & there is no MFC between mux and RAT */
                if (sAttr.info.voice_rec_info.record_direction == INCALL_RECORD_VOICE_UPLINK_DOWNLINK) {
                    codecConfig.ch_info.channels = 2;
                } else {
                   /*
                    * RAT needs to be in sync with Mux/Demux o/p.
                    * In case of only UL or DL record, Mux/Demux will provide only 1 channel o/p.
                    * If the recording being done is stereo then there will be a mismatch between RAT and Mux/Demux.
                    * which will lead to noisy clip. Hence, RAT needs to be hard-coded based on record direction.
                    */
                    codecConfig.ch_info.channels = 1;
                }
                builder->payloadRATConfig(&payload, &payloadSize, miid, &codecConfig);
                if (payloadSize && payload) {
                    status = builder->updateCustomPayload(payload, payloadSize);
                    builder->freeCustomPayload(&payload, &payloadSize);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
                        goto exit;
                    }
                }
                builder->getCustomPayload(&payload, &payloadSize);
                status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                                payload, payloadSize);
                builder->freeCustomPayload();
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "setMixerParameter failed for RAT render");
                    goto exit;
                }
                switch (sAttr.info.voice_rec_info.record_direction) {
                    case INCALL_RECORD_VOICE_UPLINK:
                        tagId = INCALL_RECORD_UPLINK;
                        break;
                    case INCALL_RECORD_VOICE_DOWNLINK:
                        tagId = INCALL_RECORD_DOWNLINK;
                        break;
                    case INCALL_RECORD_VOICE_UPLINK_DOWNLINK:
                        if (sAttr.in_media_config.ch_info.channels == 2)
                            tagId = INCALL_RECORD_UPLINK_DOWNLINK_STEREO;
                        else
                            tagId = INCALL_RECORD_UPLINK_DOWNLINK_MONO;
                        break;
                }
                status = session->setConfig(s, MODULE, tagId);
                if (status)
                    PAL_ERR(LOG_TAG, "Failed to set incall record params status = %d", status);
            }
        } else if (sAttr.type == PAL_STREAM_VOICE_UI ||
                       sAttr.type == PAL_STREAM_ASR) {
            builder->getCustomPayload(&payload, &payloadSize);
            SessionAlsaUtils::setMixerParameter(mxr,
                pcmDevIds.at(0), payload, payloadSize);
            builder->freeCustomPayload();
        } else if (sAttr.type == PAL_STREAM_ACD) {
            size_t eventPayloadSize;
            void* eventPayload = nullptr;
            status = session->getEventPayload(&eventPayload, &eventPayloadSize);
            if (status) {
                PAL_ERR(LOG_TAG, "getEventPayload failure");
                goto exit;
            }
            if (eventPayload) {
                PAL_DBG(LOG_TAG, "register ACD events");
                payload_size = sizeof(struct agm_event_reg_cfg) + eventPayloadSize;
                acd_event_cfg = (struct agm_event_reg_cfg *)calloc(1, payload_size);
                if (acd_event_cfg) {
                    uint32_t eventId = session->getEventId();
                    PAL_DBG(LOG_TAG, "eventId: %d", eventId);
                    acd_event_cfg->event_id = eventId;
                    acd_event_cfg->event_config_payload_size = eventPayloadSize;
                    acd_event_cfg->is_register = 1;
                    memcpy(acd_event_cfg->event_config_payload, eventPayload, eventPayloadSize);
                    SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                                                            txAifBackEnds[0].second.data(),
                                                            CONTEXT_DETECTION_ENGINE,
                                                            (void *)acd_event_cfg,
                                                            payload_size);
                    free(acd_event_cfg);
                } else {
                    PAL_ERR(LOG_TAG, "get acd_event_cfg instance memory allocation failed");
                    status = -ENOMEM;
                    goto exit;
                }
            } else {
                PAL_INFO(LOG_TAG, "eventPayload is NULL");
            }
        } else if (sAttr.type == PAL_STREAM_ULTRA_LOW_LATENCY) {
            status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                                txAifBackEnds[0].second.data(),
                                                TAG_STREAM_MFC_SR, &miid);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "getModuleInstanceId failed\n");
            } else {
                PAL_DBG(LOG_TAG, "ULL record, miid : %x id = %d\n", miid, pcmDevIds.at(0));
                if (isPalPCMFormat(sAttr.in_media_config.aud_fmt_id))
                    streamData.bitWidth = ResourceManager::palFormatToBitwidthLookup(sAttr.in_media_config.aud_fmt_id);
                else
                    streamData.bitWidth = sAttr.in_media_config.bit_width;
                streamData.sampleRate = sAttr.in_media_config.sample_rate;
                streamData.numChannel = sAttr.in_media_config.ch_info.channels;
                streamData.ch_info = nullptr;
                builder->payloadMFCConfig(&payload, &payloadSize, miid, &streamData);
                if (payloadSize && payload) {
                    status = builder->updateCustomPayload(payload, payloadSize);
                    builder->freeCustomPayload(&payload, &payloadSize);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
                        goto exit;
                    }
                }
                builder->getCustomPayload(&payload, &payloadSize);
                status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                        payload, payloadSize);
                builder->freeCustomPayload();
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "setMixerParameter failed");
                    goto exit;
                }
            }
        } else if (sAttr.type == PAL_STREAM_SENSOR_PCM_DATA) {
            status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                        txAifBackEnds[0].second.data(), DEVICE_ADAM, &miid);
            if (status != 0) {
                PAL_ERR(LOG_TAG,"getModuleInstanceId failed\n");
            } else {
                status = s->getAssociatedDevices(associatedDevices);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
                    goto exit;
                }
                if (associatedDevices.empty()) {
                    PAL_ERR(LOG_TAG,"No device attached\n");
                    goto exit;
                }
                status = associatedDevices[0]->getDeviceAttributes(&dAttr);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                    goto exit;
                }
                if (dAttr.id == PAL_DEVICE_IN_ULTRASOUND_MIC &&
                    dAttr.config.ch_info.channels > 1) {
                    builder->payloadDAMPortConfig(&payload, &payloadSize, miid,
                                                    dAttr.config.ch_info.channels);
                    if (payloadSize && payload) {
                        status = builder->updateCustomPayload(payload, payloadSize);
                        builder->freeCustomPayload(&payload, &payloadSize);
                        if (0 != status) {
                            PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                            goto exit;
                        }
                    }
                    builder->getCustomPayload(&payload, &payloadSize);
                    status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                                    payload, payloadSize);
                    builder->freeCustomPayload();
                    if (status != 0) {
                        PAL_ERR(LOG_TAG, "setMixerParameter failed for RAT render");
                        goto exit;
                    }
                }
            }
        }
        if (sAttr.type == PAL_STREAM_DEEP_BUFFER) {
            status = s->getAssociatedDevices(associatedDevices);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
                goto exit;
            }
            for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                    goto exit;
                }
            }
            //Setting the device orientation during stream open for HDR record.
            if ((dAttr.id == PAL_DEVICE_IN_HANDSET_MIC || dAttr.id == PAL_DEVICE_IN_SPEAKER_MIC)
                    && strstr(dAttr.custom_config.custom_key, "unprocessed-hdr-mic")) {
                s->setOrientation(session->HDRConfigKeyToDevOrientation(dAttr.custom_config.custom_key));
                PAL_DBG(LOG_TAG,"HDR record set device orientation %d", s->getOrientation());
                if (session->setConfig(s, MODULE, ORIENTATION_TAG) != 0) {
                    PAL_DBG(LOG_TAG,"HDR record setting device orientation failed");
                }
            }
        }
        // ASR logic in ::start() begins
        status = s->getAssociatedDevices(associatedDevices);
        if (0 != status) {
                PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
            goto exit;
        }
        for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAttributes(&dAttr);
            if (0 != status) {
                    PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                    goto exit;
            }
        }
        /* Silence Detection Configuration */
        if ((rm->IsSilenceDetectionEnabled()) && (!(session->IsSilenceEventRegistered())) &&
            (dAttr.id == PAL_DEVICE_IN_HANDSET_MIC || dAttr.id == PAL_DEVICE_IN_SPEAKER_MIC)) {
           /*
            *
            * 1. Register to listen at AGM level for Silence Detection Even
            * 2. Register a Callback to receive events from DSP
            * 3. Get MIID of HW_ENDPOINT_TX
            * 4. Configure PARAM_ID_SILECENCE_DETECTION payload
            *
            **/
            PAL_INFO(LOG_TAG, "Registering For Silence Detection Events \n");

            struct apm_module_param_data_t* header = nullptr;
            param_id_silence_detection_t *silence_detection_cfg = nullptr;
            size_t pad_bytes;

            event_cfg.event_id = EVENT_ID_SILENCE_DETECTION;
            event_cfg.event_config_payload_size = 0;
            event_cfg.is_register = 1;

            status  = SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                            txAifBackEnds[0].second.data(), DEVICE_HW_ENDPOINT_TX,
                            (void *)&event_cfg, sizeof(struct agm_event_reg_cfg));
            if (status) {
                PAL_ERR(LOG_TAG, "Failed Registering for SILENCE DETECTION EVENT\n");
                goto silence_ev_setup_done;
            }
            PAL_INFO(LOG_TAG, "Registered for Silence Detection Event\n");

            status = rm->registerMixerEventCallback(pcmDevIds, handleSilenceDetectionCb,
                                                                  (uint64_t)session, true);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "Failed to register DSP cb for silence detection Event");
                goto err_silence_ev_cb_reg;
            }
            PAL_INFO(LOG_TAG, "Registered CB for Silence Detection\n");

            status =  SessionAlsaUtils::getModuleInstanceId(mxr,
                            pcmDevIds.at(0), txAifBackEnds[0].second.data(),
                                                DEVICE_HW_ENDPOINT_TX, &miid);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "Error retrieving MIID for HW_ENDPOINT_TX\n");
                goto err_silence_ev_setup;
            }

            payloadSize = sizeof(struct apm_module_param_data_t)
                                + sizeof(param_id_silence_detection_t);
            pad_bytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

            payload = (uint8_t *)calloc(1, payloadSize+pad_bytes);
            if (!payload){
                PAL_ERR(LOG_TAG, "payload info calloc failed \n");
                goto err_silence_ev_setup;
            }

            header = (struct apm_module_param_data_t *)payload;
            header->module_instance_id = miid;
            header->param_id =  PARAM_ID_SILENCE_DETECTION;
            header->error_code = 0x0;
            header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);

            silence_detection_cfg = (param_id_silence_detection_t *)(payload +
                            sizeof(struct apm_module_param_data_t));
            silence_detection_cfg->enable_detection = 1;
            silence_detection_cfg->detection_duration_ms = 3000;

            PAL_INFO(LOG_TAG, "Sending Silence Detection Custom Payload\n");
            status = builder->updateCustomPayload(payload, (payloadSize + pad_bytes));
            builder->freeCustomPayload(&payload, &payloadSize);
            if (status !=0) {
                PAL_ERR(LOG_TAG, "updateCustomPayload failed for SILENCE DETECTION \n");
                goto err_silence_ev_setup;
            }
            builder->getCustomPayload(&payload, &payloadSize);
            status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                            payload, payloadSize);
            builder->freeCustomPayload();
            if (status != 0) {
                PAL_ERR(LOG_TAG, "setMixerParameter failed for Silence Detection Parameter");
                goto err_silence_ev_setup;
            }

            PAL_INFO(LOG_TAG, "Silence Detection Event Setup Complete\n");

            /* disable temporarily Silence Detection to prevent multiple registration */
            session->setSilenceEventRegistered(true);
            goto silence_ev_setup_done;

err_silence_ev_setup:
            rm->registerMixerEventCallback(pcmDevIds, handleSilenceDetectionCb,
            (uint64_t)session, false);
err_silence_ev_cb_reg:
            event_cfg.is_register = 0;
            status  = SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                            txAifBackEnds[0].second.data(), DEVICE_HW_ENDPOINT_TX,
                            (void *)&event_cfg, sizeof(struct agm_event_reg_cfg));
silence_ev_setup_done:
                status = 0;
        } // ASR logic in ::start() ends
        if (ResourceManager::isLpiLoggingEnabled()) {
            struct audio_route *audioRoute;
            status = rm->getAudioRoute(&audioRoute);
            if (!status)
                audio_route_apply_and_update_path(audioRoute, "lpi-pcm-logging");
            PAL_INFO(LOG_TAG, "LPI data logging Param ON");
            /* No error check as TAG/TKV may not required for non LPI usecases */
            session->setConfig(s, MODULE, LPI_LOGGING_ON);
        }
        break;
    case PAL_AUDIO_OUTPUT:
        if (sAttr.type == PAL_STREAM_VOICE_CALL_MUSIC) {
            if (pcmDevIds.size() == 0) {
                PAL_ERR(LOG_TAG, "frontendIDs is not available.");
                status = -EINVAL;
                goto exit;
            }
            /*if in call music plus playback configure MFC*/
            if(sAttr.info.incall_music_info.local_playback){
                status = configureInCallRxMFC(session, rm, builder);
            }
            if (0 != status) {
                PAL_INFO(LOG_TAG, "Unable to configure MFC voice call has not started %d", status);
            }
            goto exit;
        }
        if (!rxAifBackEnds.size()) {
            PAL_ERR(LOG_TAG, "rxAifBackEnds are not available");
            status = -EINVAL;
            goto exit;
        }
        if (sAttr.type == PAL_STREAM_HAPTICS && rm->IsHapticsThroughWSA()) {
            status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                    rxAifBackEnds[0].second.data(), MODULE_HAPTICS_GEN, &miid);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
                goto exit;
            }
            PAL_INFO(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
            pal_param_haptics_cnfg_t* hpCnfg = new pal_param_haptics_cnfg_t;
            if (sAttr.info.opt_stream_info.haptics_type == PAL_STREAM_HAPTICS_RINGTONE) {
                hpCnfg->mode = PAL_STREAM_HAPTICS_RINGTONE;
            } else if(sAttr.info.opt_stream_info.haptics_type == PAL_STREAM_HAPTICS_TOUCH) {
                status = session->getHapticsConfig(hpCnfg);
            }
            if (hpCnfg != nullptr && status == 0) {
                builder->payloadHapticsDevPConfig(&payload, &payloadSize,
                            miid, PARAM_ID_HAPTICS_WAVE_DESIGNER_CFG,(void *)hpCnfg);
                if (payloadSize && payload) {
                    status = builder->updateCustomPayload(payload, payloadSize);
                    builder->freeCustomPayload(&payload, &payloadSize);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
                        delete hpCnfg;
                        goto exit;
                    }
                }
                builder->getCustomPayload(&payload, &payloadSize);
                status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                        payload, payloadSize);
                builder->freeCustomPayload();
                delete hpCnfg;
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "setMixerParameter failed for Haptics wavegen");
                    goto exit;
                }
                if (sAttr.info.opt_stream_info.haptics_type == PAL_STREAM_HAPTICS_TOUCH)
                    goto exit;
            }
            else {
                PAL_ERR(LOG_TAG, "haptics config is not set");
                goto exit;
            }
        }
        status = s->getAssociatedDevices(associatedDevices);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "getAssociatedDevices Failed\n");
            goto exit;
        }
        for (int i = 0; i < associatedDevices.size();i++) {
            status = associatedDevices[i]->getDeviceAttributes(&dAttr);
            if (0 != status) {
                PAL_ERR(LOG_TAG, "get Device Attributes Failed\n");
                goto exit;
            }
            status = configureMFC(rm, sAttr, dAttr, pcmDevIds,
                        rxAifBackEnds[i].second.data(), builder);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "build MFC payload failed");
                goto exit;
            }
            builder->getCustomPayload(&payload, &payloadSize);
            if (payload) {
                status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                            payload, payloadSize);
                builder->freeCustomPayload();
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "setMixerParameter failed");
                    goto exit;
                }
            }
            if ((rm->IsChargeConcurrencyEnabled()) &&
                (dAttr.id == PAL_DEVICE_OUT_SPEAKER)) {
                status = session->NotifyChargerConcurrency(rm, true);
                if (0 == status) {
                    status = session->EnableChargerConcurrency(rm, s);
                    //Handle failure case of ICL config
                    if (0 != status) {
                        PAL_DBG(LOG_TAG, "Failed to set ICL Config status %d", status);
                        status = session->NotifyChargerConcurrency(rm, false);
                    }
                }
                /*
                    Irespective of status, Audio continues to play for success
                    status, PB continues in Buck mode otherwise play in Boost mode.
                */
                status = 0;
            }
        }
        if (PAL_DEVICE_OUT_SPEAKER == dAttr.id &&
            ((sAttr.type == PAL_STREAM_LOW_LATENCY) ||
            (sAttr.type == PAL_STREAM_PCM_OFFLOAD) ||
            (sAttr.type == PAL_STREAM_DEEP_BUFFER))) {
            // Set MSPP volume during initlization.
            if (!strcmp(dAttr.custom_config.custom_key, "mspp")) {
                status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                            rxAifBackEnds[0].second.data(), TAG_MODULE_MSPP, &miid);
                if (status != 0) {
                    PAL_ERR(LOG_TAG,"get MSPP ModuleInstanceId failed");
                    goto exit;
                }
                PAL_INFO(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));

                builder->payloadMSPPConfig(&payload, &payloadSize, miid, rm->getLinearGain().gain);
                if (payloadSize && payload) {
                    status = builder->updateCustomPayload(payload, payloadSize);
                    builder->freeCustomPayload(&payload, &payloadSize);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                        goto exit;
                    }
                }
                builder->getCustomPayload(&payload, &payloadSize);
                status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                                payload, payloadSize);
                builder->freeCustomPayload();
                status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                        rxAifBackEnds[0].second.data(), TAG_PAUSE, &miid);
                if (status != 0) {
                    PAL_ERR(LOG_TAG,"get Soft Pause ModuleInstanceId failed");
                    goto exit;
                }
                PAL_INFO(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
                builder->payloadSoftPauseConfig(&payload, &payloadSize, miid,
                                                        MSPP_SOFT_PAUSE_DELAY);
                if (payloadSize && payload) {
                    status = builder->updateCustomPayload(payload, payloadSize);
                    builder->freeCustomPayload(&payload, &payloadSize);
                }
                builder->getCustomPayload(&payload, &payloadSize);
                status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                                payload, payloadSize);
                builder->freeCustomPayload();
                if (status != 0) {
                    PAL_ERR(LOG_TAG,"setMixerParameter failed for soft Pause module");
                    goto exit;
                }
                s->setOrientation(rm->getOrientation());
                PAL_DBG(LOG_TAG,"MSPP set device orientation %d", s->getOrientation());
                if (session->setConfig(s, MODULE, ORIENTATION_TAG) != 0) {
                    PAL_DBG(LOG_TAG,"MSPP setting device orientation failed");
                }
            } else {
                pal_param_device_rotation_t rotation;
                rotation.rotation_type = rm->getOrientation() == ORIENTATION_270 ?
                                        PAL_SPEAKER_ROTATION_RL : PAL_SPEAKER_ROTATION_LR;
                status = handleDeviceRotation(rm, s, rotation.rotation_type,
                                            pcmDevIds.at(0), mxr, builder, rxAifBackEnds);
                if (status != 0) {
                    PAL_ERR(LOG_TAG,"handleDeviceRotation failed\n");
                    status = 0;
                    goto exit;
                }
            }
        }
        //set voip_rx ec ref MFC config to match with rx stream
        if (sAttr.type == PAL_STREAM_VOIP_RX) {
            status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                                rxAifBackEnds[0].second.data(),
                                                    TAG_DEVICEPP_EC_MFC, &miid);
            if (status != 0) {
                PAL_ERR(LOG_TAG,"getModuleInstanceId failed\n");
                status = 0;
                goto exit;
            }
            PAL_INFO(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
            status = s->getAssociatedDevices(associatedDevices);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
                status = 0;
                goto exit;
            }
            for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                    status = 0;
                    goto exit;
                }
                //NN NS is not enabled for BT right now, need to change bitwidth based on BT config
                //when anti howling is enabled. Currently returning success if graph does not have
                //TAG_DEVICEPP_EC_MFC tag
                if ((dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_A2DP) ||
                    (dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_BLE) ||
                    (dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_SCO)) {
                    struct pal_media_config codecConfig;
                    status = associatedDevices[i]->getCodecConfig(&codecConfig);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "getCodecConfig Failed \n");
                        status = 0;
                        goto exit;
                    }
                    streamData.sampleRate = codecConfig.sample_rate;
                    streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                    streamData.numChannel = 0xFFFF;
                } else if (dAttr.id == PAL_DEVICE_OUT_USB_DEVICE
                                || dAttr.id == PAL_DEVICE_OUT_USB_HEADSET) {
                    streamData.sampleRate =
                                (dAttr.config.sample_rate % SAMPLINGRATE_8K == 0
                                    && dAttr.config.sample_rate <= SAMPLINGRATE_48K)
                                ? dAttr.config.sample_rate : SAMPLINGRATE_48K;
                    streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                    streamData.numChannel = 0xFFFF;
                } else {
                    streamData.sampleRate = dAttr.config.sample_rate;
                    streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                    streamData.numChannel = 0xFFFF;
                }
                builder->payloadMFCConfig(&payload, &payloadSize, miid, &streamData);
                if (payloadSize && payload) {
                    status = builder->updateCustomPayload(payload, payloadSize);
                    builder->freeCustomPayload(&payload, &payloadSize);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                        status = 0;
                        goto exit;
                    }
                }
            }
            builder->getCustomPayload(&payload, &payloadSize);
            status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                            payload, payloadSize);
            builder->freeCustomPayload();
            if (status != 0) {
                PAL_ERR(LOG_TAG, "setMixerParameter failed");
                status = 0;
                goto exit;
            }
        }
        if (sAttr.type == PAL_STREAM_VOIP_RX) {
                status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                                    rxAifBackEnds[0].second.data(),
                                                                DEVICE_MFC, &miid);
            if (status != 0) {
                PAL_ERR(LOG_TAG,"getModuleInstanceId failed\n");
                status = 0;
                goto exit;
            }
            PAL_INFO(LOG_TAG, "miid : %x id = %d\n", miid, pcmDevIds.at(0));
            status = s->getAssociatedDevices(associatedDevices);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
                status = 0;
                goto exit;
            }
            for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                    status = 0;
                    goto exit;
                }
                if (dAttr.id == PAL_DEVICE_OUT_USB_DEVICE
                    || dAttr.id == PAL_DEVICE_OUT_USB_HEADSET)
                {
                    streamData.sampleRate =
                            (dAttr.config.sample_rate % SAMPLINGRATE_8K == 0
                                && dAttr.config.sample_rate <= SAMPLINGRATE_48K)
                            ? dAttr.config.sample_rate : SAMPLINGRATE_48K;
                    streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                    streamData.numChannel = 0xFFFF;
                } else {
                    streamData.sampleRate = dAttr.config.sample_rate;
                    streamData.bitWidth   = AUDIO_BIT_WIDTH_DEFAULT_16;
                    streamData.numChannel = 0xFFFF;
                }
                builder->payloadMFCConfig(&payload, &payloadSize, miid, &streamData);
                if (payloadSize && payload) {
                    status = builder->updateCustomPayload(payload, payloadSize);
                    builder->freeCustomPayload(&payload, &payloadSize);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                        status = 0;
                        goto exit;
                    }
                }
            }
            builder->getCustomPayload(&payload, &payloadSize);
            status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                        payload, payloadSize);
            builder->freeCustomPayload();
            if (status != 0) {
                PAL_ERR(LOG_TAG, "setMixerParameter failed");
                status = 0;
                goto exit;
            }
        }
        break;
    case PAL_AUDIO_INPUT | PAL_AUDIO_OUTPUT:
        std::vector<int> pcmDevRxIds;
        status = session->getFrontEndIds(pcmDevRxIds, RX_HOSTLESS);
        if (status) {
            PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevRxIds) failed %d", status);
            goto exit;
        }
        if (!rxAifBackEnds.size()) {
            PAL_ERR(LOG_TAG, "rxAifBackEnds are not available");
            status = -EINVAL;
            goto exit;
        }
        status = s->getAssociatedDevices(associatedDevices);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "getAssociatedDevices Failed");
            goto exit;
        }
        for (int i = 0; i < associatedDevices.size(); i++) {
            if (!SessionAlsaUtils::isRxDevice(
                        associatedDevices[i]->getSndDeviceId()))
                continue;
            status = associatedDevices[i]->getDeviceAttributes(&dAttr);
            if (0 != status) {
                PAL_ERR(LOG_TAG, "get Device Attributes Failed");
                goto exit;
            }
            status = configureMFC(rm, sAttr, dAttr, pcmDevRxIds,
                        rxAifBackEnds[0].second.data(), builder);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "build MFC payload failed");
                goto exit;
            }
            builder->getCustomPayload(&payload, &payloadSize);
            if (payload) {
                if (!pcmDevRxIds.size()) {
                    PAL_ERR(LOG_TAG, "pcmDevRxIds not found.");
                    status = -EINVAL;
                    builder->freeCustomPayload();
                    goto exit;
                }
                status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevRxIds.at(0),
                                                            payload, payloadSize);
                builder->freeCustomPayload();
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "setMixerParameter failed");
                    goto exit;
                }
            }
            if ((rm->IsChargeConcurrencyEnabled()) &&
                (dAttr.id == PAL_DEVICE_OUT_SPEAKER)) {
                status = session->NotifyChargerConcurrency(rm, true);
                if (0 == status) {
                    status = session->EnableChargerConcurrency(rm, s);
                    //Handle failure case of ICL config
                    if (0 != status) {
                        PAL_DBG(LOG_TAG, "Failed to set ICL Config status %d", status);
                        status = session->NotifyChargerConcurrency(rm, false);
                    }
                }
                status = 0;
            }
        }
        break;
    }
exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int32_t pcmPluginConfigSetConfigStop(Stream* s, void* pluginPayload)
{
    int status = 0;
    std::vector<int> pcmDevIds;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    std::shared_ptr<ResourceManager> rm = nullptr;
    struct pal_stream_attributes sAttr = {};
    struct pal_device dAttr = {};
    struct agm_event_reg_cfg event_cfg = {};
    SessionAlsaPcm* session = nullptr;
    struct mixer* mxr = nullptr;
    int payload_size = 0;
    int tagId = 0;
    int DeviceId = 0;

    PAL_DBG(LOG_TAG, "Enter");
    rm = ResourceManager::getInstance();
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "stream get attributes failed");
        return status;
    }

    session = reinterpret_cast<SessionAlsaPcm*>(pluginPayload);
    if (session == nullptr) {
        PAL_ERR(LOG_TAG, "SessionAlsaPcm ptr is null\n");
        goto exit;
    }
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }

    rxAifBackEnds = session->getRxBEVecRef();
    txAifBackEnds = session->getTxBEVecRef();
    status = session->getFrontEndIds(pcmDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }

    switch (sAttr.direction) {
        case PAL_AUDIO_INPUT:
            PAL_DBG(LOG_TAG, "case PAL_AUDIO_INPUT:\n");
            if (ResourceManager::isLpiLoggingEnabled()) {
                struct audio_route *audioRoute;

                status = rm->getAudioRoute(&audioRoute);
                if (!status)
                    audio_route_reset_and_update_path(audioRoute, "lpi-pcm-logging");
            }
            if (rm->IsSilenceDetectionEnabled() && session->IsSilenceEventRegistered()) {

                status = s->getAssociatedDevices(associatedDevices);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
                    goto exit;
                }
                for (int i = 0; i < associatedDevices.size();i++) {
                    status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                        goto exit;
                    }
                }
                if (dAttr.id != PAL_DEVICE_IN_HANDSET_MIC && dAttr.id != PAL_DEVICE_IN_SPEAKER_MIC) {
                    status = 0;
                    break;
                }

                PAL_INFO(LOG_TAG, "De-registering For Silence Detection Events\n");
                event_cfg.event_id = EVENT_ID_SILENCE_DETECTION;
                event_cfg.event_config_payload_size = 0;
                event_cfg.is_register = 0;
                status  = SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                             txAifBackEnds[0].second.data(), DEVICE_HW_ENDPOINT_TX,
                             (void *)&event_cfg, sizeof(struct agm_event_reg_cfg));
                if (status)
                    PAL_ERR(LOG_TAG, "Unable to deregister SILENCE DETECTION EVENT\n");

               status = rm->registerMixerEventCallback(pcmDevIds, handleSilenceDetectionCb, (uint64_t)session, false);
               if (status != 0) {
                    PAL_ERR(LOG_TAG, "Failed to deregister  silence detection Callback to rm");
               }
               /* re-enable Silence Detection to allow registrations */
               session->setSilenceEventRegistered(false);
            }
            break;
        case PAL_AUDIO_OUTPUT:
            PAL_DBG(LOG_TAG, "case PAL_AUDIO_OUTPUT:\n");
            if (session->getIsPauseRegistrationDone()) {
                // Stream supports Soft Pause and was registered with RM
                // successfully. Thus Deregister callback for Soft Pause
                payload_size = sizeof(struct agm_event_reg_cfg);
                memset(&event_cfg, 0, sizeof(event_cfg));
                event_cfg.event_id = EVENT_ID_SOFT_PAUSE_PAUSE_COMPLETE;
                event_cfg.event_config_payload_size = 0;
                event_cfg.is_register = 0;

                if (!pcmDevIds.size()) {
                    PAL_ERR(LOG_TAG, "frontendIDs are not available");
                    status = -EINVAL;
                    goto exit;
                }
                if (!rxAifBackEnds.size()) {
                    PAL_ERR(LOG_TAG, "rxAifBackEnds are not available");
                    status = -EINVAL;
                    goto exit;
                }
                status = SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                        rxAifBackEnds[0].second.data(), TAG_PAUSE, (void *)&event_cfg,
                        payload_size);
                if (status == 0 || rm->getSoundCardState() == CARD_STATUS_OFFLINE) {
                    session->setIsPauseRegistrationDone(false);
                } else {
                    // Not a fatal error
                    PAL_ERR(LOG_TAG, "Pause deregistration failed");
                    status = 0;
                }
            }
            break;
        case PAL_AUDIO_INPUT | PAL_AUDIO_OUTPUT:
            break;
    }

    if (sAttr.type == PAL_STREAM_VOICE_UI) {
        payload_size = sizeof(struct agm_event_reg_cfg);
        uint32_t svaMiid;
        svaMiid = session->getsvaMiid();
        memset(&event_cfg, 0, sizeof(event_cfg));
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 0;
        event_cfg.event_id = s->getCallbackEventId();
        event_cfg.module_instance_id = svaMiid;
        if (!pcmDevIds.size()) {
            PAL_ERR(LOG_TAG, "pcmDevIds not found.");
            status = -EINVAL;
            goto exit;
        }
        SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            (void *)&event_cfg, payload_size);

        memset(&event_cfg, 0, sizeof(event_cfg));
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 0;
        event_cfg.event_id = EVENT_ID_SH_MEM_PUSH_MODE_EOS_MARKER;
        tagId = SHMEM_ENDPOINT;
        SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            txAifBackEnds[0].second.data(), tagId, (void *)&event_cfg,
            payload_size);

    } else if (sAttr.type == PAL_STREAM_ULTRASOUND && session->getRegisterForEvents()) {
        payload_size = sizeof(struct agm_event_reg_cfg);
        std::vector<int> pcmDevTxIds;
        status = session->getFrontEndIds(pcmDevTxIds, TX_HOSTLESS);
        if (status) {
            PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevTxIds) failed %d", status);
            goto exit;
        }
        memset(&event_cfg, 0, sizeof(event_cfg));
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 0;
        event_cfg.event_id = EVENT_ID_GENERIC_US_DETECTION;
        tagId = ULTRASOUND_DETECTION_MODULE;
        DeviceId = pcmDevTxIds.at(0);
        session->setRegisterForEvents(false);
        SessionAlsaUtils::registerMixerEvent(mxr, DeviceId,
                txAifBackEnds[0].second.data(), tagId, (void *)&event_cfg,
                payload_size);
    } else if (sAttr.type == PAL_STREAM_ACD || sAttr.type == PAL_STREAM_ASR) {
        uint32_t eventId;
        void* eventPayload = nullptr;
        session->getEventPayload(&eventPayload, nullptr);

        if (eventPayload == nullptr) {
            PAL_INFO(LOG_TAG, "eventPayload is NULL");
            goto exit;
        }
        payload_size = sizeof(struct agm_event_reg_cfg);
        memset(&event_cfg, 0, sizeof(event_cfg));
        eventId = session->getEventId();
        event_cfg.event_id = eventId;
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 0;
        tagId = sAttr.type == PAL_STREAM_ACD ? CONTEXT_DETECTION_ENGINE :
                                        TAG_MODULE_ASR;
        if (!txAifBackEnds.empty()) {
            if (!pcmDevIds.size()) {
                PAL_ERR(LOG_TAG, "pcmDevIds not found.");
                status = -EINVAL;
                goto exit;
            }
            SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                    txAifBackEnds[0].second.data(), tagId, (void *)&event_cfg,
                    payload_size);
        }
    } else if(sAttr.type == PAL_STREAM_CONTEXT_PROXY) {
        status = register_asps_event(0, session, mxr);
    } else if(sAttr.type == PAL_STREAM_HAPTICS &&
            sAttr.info.opt_stream_info.haptics_type == PAL_STREAM_HAPTICS_TOUCH) {
        payload_size = sizeof(struct agm_event_reg_cfg);

        memset(&event_cfg, 0, sizeof(event_cfg));
        event_cfg.event_config_payload_size = 0;
        event_cfg.is_register = 0;
        event_cfg.event_id = EVENT_ID_WAVEFORM_STATE;
        SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                rxAifBackEnds[0].second.data(), MODULE_HAPTICS_GEN, (void *)&event_cfg,
                payload_size);
    }
exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int register_asps_event(uint32_t reg, SessionAlsaPcm* session, struct mixer* mxr)
{
    int32_t status = 0;
    struct agm_event_reg_cfg *event_cfg = nullptr;
    uint32_t payload_size = sizeof(struct agm_event_reg_cfg);
    std::vector<int> pcmDevIds;

    PAL_DBG(LOG_TAG, "Enter");
    status = session->getFrontEndIds(pcmDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }
    event_cfg = new agm_event_reg_cfg;
    event_cfg->event_config_payload_size = 0;
    event_cfg->is_register = reg;
    event_cfg->event_id = EVENT_ID_ASPS_GET_SUPPORTED_CONTEXT_IDS;
    event_cfg->module_instance_id = ASPS_MODULE_INSTANCE_ID;
    SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            (void *)event_cfg, payload_size);

    event_cfg->event_id = EVENT_ID_ASPS_SENSOR_REGISTER_REQUEST;
    SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            (void *)event_cfg, payload_size);

    event_cfg->event_id = EVENT_ID_ASPS_SENSOR_DEREGISTER_REQUEST;
    SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            (void *)event_cfg, payload_size);

    event_cfg->event_id = EVENT_ID_ASPS_CLOSE_ALL;
    SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
            (void *)event_cfg, payload_size);
    delete event_cfg;
exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int32_t configureInCallRxMFC(SessionAlsaPcm* session, std::shared_ptr<ResourceManager> rm, PayloadBuilder* builder)
{
    int32_t status = 0;
    std::vector <std::shared_ptr<Device>> devices;
    std::shared_ptr<Device> rxDev = nullptr;
    struct pal_device dattr = {};
    sessionToPayloadParam deviceData = {};
    typename std::vector<std::shared_ptr<Device>>::iterator iter;

    PAL_DBG(LOG_TAG, "Enter");
    status = rm->getActiveVoiceCallDevices(devices);
    if(devices.empty()){
        PAL_ERR(LOG_TAG, "Cannot start an in Call stream without a running voice call");
        status = -EINVAL;
        goto exit;
    }
    for (iter = devices.begin(); iter != devices.end(); iter++) {
        if ((*iter) && rm->isOutputDevId((*iter)->getSndDeviceId())) {
            status = (*iter)->getDeviceAttributes(&dattr);
            if (status) {
                PAL_ERR(LOG_TAG,"get Device attributes failed\n");
                status = -EINVAL;
                goto exit;
            }
            deviceData.bitWidth = dattr.config.bit_width;
            deviceData.sampleRate = dattr.config.sample_rate;
            deviceData.numChannel = dattr.config.ch_info.channels;
            deviceData.ch_info = nullptr;
            status = reconfigureModule(session, builder, PER_STREAM_PER_DEVICE_MFC, "ZERO", &deviceData);
            break;
        }
    }
exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
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
    if (!timenow) {
        PAL_ERR(LOG_TAG, "failed to initialize timenow struct\n");
        return;
    }
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