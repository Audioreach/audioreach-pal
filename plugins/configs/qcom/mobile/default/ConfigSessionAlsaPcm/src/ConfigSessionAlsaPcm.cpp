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
 */

#define LOG_TAG "PAL: libsession_pcm_config"

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
#include "mma_api.h"
#include "PayloadBuilder.h"
#include "SessionAR.h"
#include "ConfigSessionAlsaPcm.h"
#include "ConfigSessionUtils.h"

// ASR handlecb def supports
#include "asr_module_calibration_api.h"
#include "sdz_api.h"
#include "nmt_module_calibration_api.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/klog.h>        /* Definition of SYSLOG_* constants */
#include <time.h>

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
        case PAL_PLUGIN_PRE_RECONFIG:
            status = pcmPluginPreReconfig(stream, pluginPayload);
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

int32_t pcmPluginPreReconfig(Stream* s, void* pluginPayload) {
    int status = 0;
    struct ReconfigPluginPayload* reconfigPld = nullptr;

    PAL_DBG(LOG_TAG,"Enter");

    if (!pluginPayload) {
        PAL_ERR(LOG_TAG, "plugin Payload is null");
        return -EINVAL;
    }
    reconfigPld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);

    if (!reconfigPld->config_ctrl.compare("silent_detection")) {
        status = pcmSilenceDetectionConfig(SD_DISCONNECT, &reconfigPld->dAttr, pluginPayload);
    }
    return status;
}

uint32_t getMiid(SessionAlsaPcm* session, uint64_t eventId, mixer* mxr,
                 std::vector<std::pair<int32_t, std::string>> txAifBackEnds, std::vector<int> pcmDevIds) {

    uint32_t miid = 0;
    int tagId = 0;
    int status = 0;

    switch (eventId) {
        case EVENT_ID_MMA_DETECTION_EVENT:
        case EVENT_ID_DETECTION_ENGINE_GENERIC_INFO:
            miid = session->getsvaMiid();
            break;
        case EVENT_ID_NMT_STATUS:
            miid = session->getNmtMiid();
            break;
        case EVENT_ID_SH_MEM_PUSH_MODE_EOS_MARKER:
            tagId = SHMEM_ENDPOINT;
            break;
        case EVENT_ID_ACD_DETECTION_EVENT :
            tagId = CONTEXT_DETECTION_ENGINE;
            break;
        case EVENT_ID_ASR_OUTPUT :
            tagId = TAG_MODULE_ASR;
            break;
        case EVENT_ID_SDZ_OUTPUT:
            tagId = TAG_MODULE_SDZ;
            break;
        case EVENT_ID_SH_MEM_PULL_PUSH_MODE_WATERMARK:
            tagId = SHMEM_ENDPOINT;
            break;
        default :
            PAL_ERR(LOG_TAG, "Event id %x not handled!!!", eventId);
    }

    if (miid == 0 && tagId != 0) {
        status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                        txAifBackEnds[0].second.data(), tagId, &miid);
        if (status) {
            PAL_ERR(LOG_TAG, "Failed to get tag info %x, status = %d", tagId, status);
        }
    }
    return miid;
}

int32_t handleEventRegistration(SessionAlsaPcm* session, int isRegister, mixer* mxr,
                                std::vector<std::pair<int32_t, std::string>> txAifBackEnds, std::vector<int> pcmDevIds) {

    int32_t status = 0;
    uint32_t miid = 0;
    struct agm_event_reg_cfg *vai_event_cfg = nullptr;
    int payload_size = 0;
    std::vector<SessionAlsaPcm::eventPayload *> payloadListParam;

    session->getEventPayload(payloadListParam);
    for (int i = 0; i < payloadListParam.size(); i++) {
        payload_size = sizeof(struct agm_event_reg_cfg) +
                        payloadListParam[i]->payloadSize;
        vai_event_cfg = (struct agm_event_reg_cfg *)calloc(1, payload_size);
        if (!vai_event_cfg) {
            PAL_ERR(LOG_TAG, "Failed to allocate memory for acd event config");
            status = -EINVAL;
            goto exit;
        }
        miid = getMiid(session, payloadListParam[i]->eventId, mxr, txAifBackEnds, pcmDevIds);
        if (miid == 0) {
            PAL_ERR(LOG_TAG, "Failed to get miid!!!");
            return -EINVAL;
        }
        vai_event_cfg->event_config_payload_size = payloadListParam[i]->payloadSize;
        vai_event_cfg->is_register = isRegister;
        vai_event_cfg->event_id = payloadListParam[i]->eventId;
        vai_event_cfg->module_instance_id = miid;
        if (payloadListParam[i]->payloadSize > 0) {
            memcpy(vai_event_cfg->event_config_payload,
                    payloadListParam[i]->payload,
                    payloadListParam[i]->payloadSize);
        }
        SessionAlsaUtils::registerMixerEvent(mxr, pcmDevIds.at(0),
                                (void *)vai_event_cfg, payload_size);
        free(vai_event_cfg);
        miid = 0;
    }
exit:
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
    std::vector<int> pcmDevIds;
    int DeviceId = 0;
    int tagId = 0;
    int payload_size = 0;
    uint32_t tag = 0;
    std::vector<uint32_t> MIIDs;

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

    if (sAttr.type == PAL_STREAM_VOICE_UI ||
        sAttr.type == PAL_STREAM_ACD ||
        sAttr.type == PAL_STREAM_ASR ||
        sAttr.type == PAL_STREAM_CALL_TRANSLATION) {
        handleEventRegistration(session, 1, mxr, txAifBackEnds, pcmDevIds);
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
    } else if (sAttr.info.opt_stream_info.isBitPerfect) {
        PAL_DBG(LOG_TAG, "Config not needed for BitPerfect Playback");
        goto exit;
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
                (sAttr.type != PAL_STREAM_ULTRA_LOW_LATENCY) &&
                (sAttr.type != PAL_STREAM_COMMON_PROXY) &&
                (sAttr.type != PAL_STREAM_CALL_TRANSLATION)) {
                /* Get MFC MIID and configure to match to stream config */
                /* This has to be done after sending all mixer controls and before connect */
                if (sAttr.type != PAL_STREAM_VOICE_CALL_RECORD)
                    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                                                txAifBackEnds[0].second.data(),
                                                                TAG_STREAM_MFC_SR, MIIDs);
                else
                    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),
                                                                "ZERO", TAG_STREAM_MFC_SR, MIIDs);
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
                    goto exit;
                }
                if (sAttr.type != PAL_STREAM_VOICE_CALL_RECORD) {
                    for (const auto& miids : MIIDs) {
                        PAL_ERR(LOG_TAG, "miid : %x id = %d, data %s\n", miids,
                        pcmDevIds.at(0), txAifBackEnds[0].second.data());
                    }
                } else {
                    for (const auto& miids : MIIDs) {
                        PAL_ERR(LOG_TAG, "miid : %x id = %d\n", miids, pcmDevIds.at(0));
                    }
                }

                if (isPalPCMFormat(sAttr.in_media_config.aud_fmt_id))
                    streamData.bitWidth =
                        ResourceManager::palFormatToBitwidthLookup(sAttr.in_media_config.aud_fmt_id);
                else
                    streamData.bitWidth = sAttr.in_media_config.bit_width;
                streamData.sampleRate = sAttr.in_media_config.sample_rate;
                streamData.numChannel = sAttr.in_media_config.ch_info.channels;
                streamData.ch_info = nullptr;
                for (const auto& miids : MIIDs) {
                    builder->payloadMFCConfig(&payload, &payloadSize, miids, &streamData);
                    if (payloadSize && payload) {
                        status = builder->updateCustomPayload(payload, payloadSize);
                        builder->freeCustomPayload(&payload, &payloadSize);
                        if (0 != status) {
                            PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
                            goto exit;
                        }
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
                if (sAttr.type == PAL_STREAM_VOIP_TX) {
                    bool isVoiceActive = false;
                    bool isTranslationActive = false;
                    pal_stream_attributes streamAttr = {};
                    for (auto& stream_itr: rm->getActiveStreamList()) {
                        PAL_DBG(LOG_TAG, ": Looking for active Voice/Voip call for configuring the Mux-Demux module.");
                        stream_itr->getStreamAttributes(&streamAttr);
                        if (streamAttr.type == PAL_STREAM_VOICE_CALL) {
                            isVoiceActive = true;
                        } else if (streamAttr.type == PAL_STREAM_CALL_TRANSLATION) {
                            isTranslationActive = true;
                        }
                    }
                    if (isVoiceActive && isTranslationActive) {
                        tag = MUX_DEMUX_VOICE;
                        PAL_DBG(LOG_TAG, "ongoing voice with Call Translation found, set TKV value as :%d", tag);
                        status = session->setConfig(s, MODULE, tag);
                        if (status) {
                            PAL_ERR(LOG_TAG, "Failed setconfig for mux-demux tag = %d", status);
                            goto set_mixer;
                        }
                    } else {
                        PAL_ERR(LOG_TAG, "Cannot set the mux-demux tag, as ongoing voice with Call Translation not found");
                        goto set_mixer;
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
                       sAttr.type == PAL_STREAM_ASR ||
                       sAttr.type == PAL_STREAM_ACD) {
            builder->getCustomPayload(&payload, &payloadSize);
            SessionAlsaUtils::setMixerParameter(mxr,
                pcmDevIds.at(0), payload, payloadSize);
            builder->freeCustomPayload();
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
                if (rm->isWNRModuleEnabled())
                {
                    status = session->enableDisableWnrModule(s);
                    PAL_DBG(LOG_TAG, "Enabling WNR module status: %d", status);
                    status = 0;
                }
            }
        }
        if (sAttr.type == PAL_STREAM_CALL_TRANSLATION) {
            status = configureCallTranslationModules(s, builder, mxr, session, rm);
            if (status) {
                PAL_ERR(LOG_TAG, "Failed to set call translation modules, status = %d", status);
                goto exit;
            }
            // configure the voice and voip mux_demux using tag.
            for (auto& stream_itr: rm->getActiveStreamList()) {
                PAL_DBG(LOG_TAG, ": Looking for active Voice/Voip call for configuring the Mux-Demux module.");
                stream_itr->getStreamAttributes(&sAttr);
                if (sAttr.type == PAL_STREAM_VOICE_CALL) {
                    tag = MUX_DEMUX_VOICE;
                    break;
                } else if (sAttr.type == PAL_STREAM_VOIP_TX) {
                    tag = MUX_DEMUX_VOIP;
                    break;
                }
            }
            status = session->setConfig(s, MODULE, tag);
            if (status) {
                PAL_ERR(LOG_TAG, "Failed to set mux-demux tag data, status = %d", status);
                goto exit;
            }
            goto exit;
        }

        if (rm->IsSilenceDetectionEnabledPcm() && sAttr.type != PAL_STREAM_VOICE_CALL_RECORD) {
            status = s->getAssociatedDevices(associatedDevices);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"getAssociatedDevices Failed for Silence Detection\n");
                goto silence_det_setup_done;
            }
            for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"get Device Attributes Failed for Silence Detection\n");
                    goto silence_det_setup_done;
                }
            }

            if (dAttr.id == PAL_DEVICE_IN_HANDSET_MIC || dAttr.id == PAL_DEVICE_IN_SPEAKER_MIC) {
                (void) enableSilenceDetection(rm, mxr, pcmDevIds,
                                txAifBackEnds[0].second.data(), (uint64_t)session);
                                ppld->session = session;
                ppld->builder = reinterpret_cast<void*>(builder);
                status = pcmSilenceDetectionConfig(SD_SETPARAM, nullptr, ppld);
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "Enable param failed for Silence Detection\n");
                    (void) disableSilenceDetection(rm, mxr, pcmDevIds,
                                    txAifBackEnds[0].second.data(), (uint64_t)session);
                    goto silence_det_setup_done;
                }
            }
        }
silence_det_setup_done:
        status = 0;
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
                PAL_ERR(LOG_TAG, "Unable to configure MFC, status = %d", status);
            }
            bool isCallActive = false;
            for (auto& stream_itr: rm->getActiveStreamList()) {
                PAL_DBG(LOG_TAG, ": Looking for active Voice/Voip call for configuring the ICMD Mux-Demux module.");
                stream_itr->getStreamAttributes(&sAttr);
                if (sAttr.type == PAL_STREAM_VOICE_CALL) {
                    PAL_DBG(LOG_TAG, ": Found Voice Call. Configure Mux/Demux for Voice");
                    tag = MUX_DEMUX_VOICE;
                    isCallActive = true;
                    break;
                } else if (sAttr.type == PAL_STREAM_VOIP_RX) {
                    PAL_DBG(LOG_TAG, ": Found VoIP Call. Configure Mux/Demux for VoIP");
                    tag = MUX_DEMUX_VOIP;
                    isCallActive = true;
                    break;
                }
            }
            if (isCallActive) {
                status = session->setConfig(s, MODULE, tag);
                if (status) {
                    PAL_ERR(LOG_TAG, "Failed to set mux-demux tag data, status = %d", status);
                    goto exit;
                }
            } else {
                PAL_DBG(LOG_TAG, ": No active Voice or VoIP call found. Skipping setConfig.");
            }
            goto exit;
        }
        if (sAttr.type == PAL_STREAM_CALL_TRANSLATION) {
            status = configureCallTranslationModules(s, builder, mxr, session, rm);
            if (status) {
                PAL_ERR(LOG_TAG, "Failed to set call RX path translation modules, status = %d", status);
                goto exit;
            }
            status = configureCallTranslationRxDeviceMFC(builder, mxr, session, rm);
            if (status) {
                PAL_ERR(LOG_TAG, "Failed to configure translation rx device mfc, status = %d", status);
                goto exit;
            }
            for (auto& stream_itr: rm->getActiveStreamList()) {
                PAL_DBG(LOG_TAG, ": Looking for active Voice/Voip call for configuring the Mux-Demux module.");
                stream_itr->getStreamAttributes(&sAttr);
                if (sAttr.type == PAL_STREAM_VOICE_CALL) {
                    tag = MUX_DEMUX_VOICE;
                    break;
                } else if (sAttr.type == PAL_STREAM_VOIP_RX) {
                    tag = MUX_DEMUX_VOIP;
                    break;
                }
            }
            status = session->setConfig(s, MODULE, tag);
            if (status) {
                PAL_ERR(LOG_TAG, "Failed to set mux-demux tag data, status = %d", status);
                goto exit;
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
            }

            if (hpCnfg != nullptr && status == 0) {
                builder->payloadHapticsDevPConfig(&payload, &payloadSize,
                            miid, PARAM_ID_HAPTICS_WAVE_DESIGNER_CFG_V2,(void *)hpCnfg);
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
            ((sAttr.type == PAL_STREAM_PCM_OFFLOAD) ||
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
        if (sAttr.type == PAL_STREAM_VOIP_RX) {
            bool isVoiceActive = false;
            bool isTranslationActive = false;
            pal_stream_attributes streamAttr = {};
            for (auto& stream_itr: rm->getActiveStreamList()) {
                PAL_DBG(LOG_TAG, ": Looking for active Voice/Voip call for configuring the Mux-Demux module.");
                stream_itr->getStreamAttributes(&streamAttr);
                if (streamAttr.type == PAL_STREAM_VOICE_CALL) {
                    isVoiceActive = true;
                } else if (streamAttr.type == PAL_STREAM_CALL_TRANSLATION) {
                    isTranslationActive = true;
                }
            }
            if (isVoiceActive && isTranslationActive) {
                tag = MUX_DEMUX_VOICE;
                PAL_DBG(LOG_TAG, "ongoing voice with Call Translation found, set TKV value as :%d", tag);
                status = session->setConfig(s, MODULE, tag);
                if (status) {
                    PAL_ERR(LOG_TAG, "Failed setconfig for mux-demux tag = %d", status);
                    goto set_mixer;
                } else {
                    PAL_ERR(LOG_TAG, "Cannot set the mux-demux tag, as ongoing voice with Call Translation not found");
                    goto set_mixer;
                }
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
            if (rm->IsSilenceDetectionEnabledPcm() && sAttr.type != PAL_STREAM_VOICE_CALL_RECORD) {
                status = s->getAssociatedDevices(associatedDevices);
                if (0 != status) {
                    PAL_ERR(LOG_TAG, "getAssociatedDevices Failed for Silence Detection\n");
                    goto silence_det_setup_done;
                }

                for (int i=0; i < associatedDevices.size(); i++) {
                    status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG, "getDeviceAttributes failed for Silence Detection\n");
                        goto silence_det_setup_done;
                    }
                }

                if (dAttr.id == PAL_DEVICE_IN_HANDSET_MIC || dAttr.id == PAL_DEVICE_IN_SPEAKER_MIC) {
                    (void) disableSilenceDetection(rm, mxr, pcmDevIds,
                              txAifBackEnds[0].second.data(), (uint64_t)session);
                }
silence_det_setup_done:
                status = 0;
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

    if (sAttr.type == PAL_STREAM_VOICE_UI ||
        sAttr.type == PAL_STREAM_ACD ||
        sAttr.type == PAL_STREAM_ASR ||
        sAttr.type == PAL_STREAM_CALL_TRANSLATION) {
        handleEventRegistration(session, 0, mxr, txAifBackEnds, pcmDevIds);
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

int32_t configureCallTranslationModules(Stream* s, PayloadBuilder* builder, struct mixer *mxr, SessionAlsaPcm* session, std::shared_ptr<ResourceManager> rm) {
    uint8_t* paramData = NULL;
    size_t paramSize = 0;
    uint32_t miid = 0;
    int status;
    std::vector<int> pcmDevIds;
    pal_asr_config* asr_payload;
    pal_tts_config* tts_payload;
    pal_nmt_config* nmt_payload;

    PAL_DBG(LOG_TAG, "Enter");
    status = session->getFrontEndIds(pcmDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }

    asr_payload = &s->callTranslationConfigPayload->asr_module_config;

    PAL_DBG(LOG_TAG, ": asr_payload : input_language_code=%d, output_language_code=%d, enable_language_detection=%d, enable_translation=%d,"
                     "enable_continuous_mode=%d, enable_partial_transcription=%d, threshold=%d, timeout_duration=%d, vad_hangover_duration=%d",
                     asr_payload->input_language_code, asr_payload->output_language_code, asr_payload->enable_language_detection,
                     asr_payload->enable_translation, asr_payload->enable_continuous_mode, asr_payload->enable_partial_transcription,
                     asr_payload->threshold, asr_payload->timeout_duration, asr_payload->silence_detection_duration);

    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),"ZERO", TRANSLATION_ASR, &miid);

    if (status) {
        PAL_ERR(LOG_TAG, "getModuleInstanceId failed %d", status);
        goto exit;
    }
    builder->payloadASRConfig(&paramData, &paramSize, miid, asr_payload);
    if (paramSize && paramData) {
        status = builder->updateCustomPayload(paramData, paramSize);
        builder->freeCustomPayload(&paramData, &paramSize);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
            goto exit;
        }
    }
    builder->getCustomPayload(&paramData, &paramSize);

    tts_payload = &s->callTranslationConfigPayload->tts_module_config;

    PAL_DBG(LOG_TAG, "tts_payload : language_code=%d, speech_format=%d",
                     tts_payload->language_code, tts_payload->speech_format);

    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),"ZERO", TRANSLATION_TTS, &miid);
    if (status) {
        PAL_ERR(LOG_TAG, "getModuleInstanceId failed %d", status);
        goto exit;
    }
    builder->payloadTTSConfig(&paramData, &paramSize, miid, tts_payload);
    if (paramSize && paramData) {
        status = builder->updateCustomPayload(paramData, paramSize);
        builder->freeCustomPayload(&paramData, &paramSize);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
            goto exit;
        }
    }
    builder->getCustomPayload(&paramData, &paramSize);

    nmt_payload = &s->callTranslationConfigPayload->nmt_module_config;
    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),"ZERO", TRANSLATION_NMT, &miid);

    if (status) {
        PAL_ERR(LOG_TAG, "getModuleInstanceId failed %d", status);
        goto exit;
    }
    builder->payloadNMTConfig(&paramData, &paramSize, miid, nmt_payload);
    if (paramSize && paramData) {
        status = builder->updateCustomPayload(paramData, paramSize);
        builder->freeCustomPayload(&paramData, &paramSize);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
                goto exit;
        }
    }
    builder->getCustomPayload(&paramData, &paramSize);

    if (paramSize) {
        status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0), paramData, paramSize);
        builder->freeCustomPayload();
    }
    if (status) {
        PAL_ERR(LOG_TAG, "setmixer, status = %d", status);
        goto exit;
    }

exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int32_t configureCallTranslationRxDeviceMFC(PayloadBuilder* builder, struct mixer *mxr, SessionAlsaPcm* session, std::shared_ptr<ResourceManager> rm) {
    uint8_t* paramData = NULL;
    size_t paramSize = 0;
    uint32_t miid = 0;
    pal_stream_attributes sAttr = {};
    std::vector<std::shared_ptr<Device>> associatedDevices;
    struct pal_device dAttr = {};
    sessionToPayloadParam deviceData = {};
    int status;
    std::vector<int> pcmDevIds;

    PAL_DBG(LOG_TAG, "Enter");
    status = session->getFrontEndIds(pcmDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }
    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevIds.at(0),"ZERO", DEVICE_MFC, &miid);
    if (status) {
        PAL_ERR(LOG_TAG, "getModuleInstanceId failed %d", status);
        goto exit;
    }
    for (auto& stream_itr: rm->getActiveStreamList()) {
        PAL_DBG(LOG_TAG, ": Looking for active Voice/Voip call for configuring the Device MFC.");
        stream_itr->getStreamAttributes(&sAttr);
        if (sAttr.type == PAL_STREAM_VOICE_CALL || sAttr.type == PAL_STREAM_VOIP_RX) {
            status = stream_itr->getAssociatedDevices(associatedDevices);
            break;
        }
    }
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
        if (dAttr.id > PAL_DEVICE_IN_MIN && dAttr.id < PAL_DEVICE_IN_MAX) {
            PAL_DBG(LOG_TAG,"Input device, skip\n");
            continue;
        }
        deviceData.bitWidth = dAttr.config.bit_width;
        deviceData.sampleRate = dAttr.config.sample_rate;
        deviceData.numChannel = dAttr.config.ch_info.channels;
        deviceData.ch_info = nullptr;
        builder->payloadMFCConfig(&paramData, &paramSize, miid, &deviceData);
        if (paramSize && paramData) {
            PAL_DBG(LOG_TAG, "customPayload address %pK and size %zu", paramData,paramSize);
            status = builder->updateCustomPayload(paramData, paramSize);
            builder->freeCustomPayload(&paramData, &paramSize);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                status = 0;
                goto exit;
            }
        }
    }
    builder->getCustomPayload(&paramData, &paramSize);
    status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                paramData, paramSize);
    builder->freeCustomPayload();

    if (status != 0) {
        PAL_ERR(LOG_TAG, "setMixerParameter failed");
        status = 0;
        goto exit;
    }
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

int pcmSilenceDetectionConfig(uint8_t config, pal_device *dAttr,  void * pluginPayload) {
    int status = 0;
    uint32_t miid = 0;
    size_t pad_bytes = 0, payloadSize = 0;
    uint8_t* payload = NULL;
    struct apm_module_param_data_t* header = NULL;
    param_id_silence_detection_t *silence_detection_cfg = NULL;
    PluginPayload* ppld = nullptr;
    SessionAlsaPcm* session = nullptr;
    struct mixer* mxr = nullptr;
    PayloadBuilder* builder = nullptr;
    std::vector<int> pcmDevIds;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    std::shared_ptr<ResourceManager> rm = nullptr;

    ppld = reinterpret_cast<PluginPayload*>(pluginPayload);
    builder = reinterpret_cast<PayloadBuilder*>(ppld->builder);
    session = static_cast<SessionAlsaPcm*>(ppld->session);
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);

    if (!rm->IsSilenceDetectionEnabledPcm())
        return 0;

    if (config != SD_SETPARAM) {
        PAL_ERR(LOG_TAG, "Invalid config to enable Silence Detection \n");
        return -EINVAL;
    }

    status = session->getFrontEndIds(pcmDevIds, TX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }

    txAifBackEnds = session->getTxBEVecRef();

    status =  SessionAlsaUtils::getModuleInstanceId(mxr,
        pcmDevIds.at(0), txAifBackEnds[0].second.data(), DEVICE_HW_ENDPOINT_TX, &miid);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "Error retrieving MIID for HW_ENDPOINT_TX\n");
        return -SD_ENABLE;
    }
    payloadSize = sizeof(struct apm_module_param_data_t)+sizeof(param_id_silence_detection_t);
    pad_bytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payload = (uint8_t *)calloc(1, payloadSize+pad_bytes);
    if (!payload){
        PAL_ERR(LOG_TAG, "payload info calloc failed \n");
        return -SD_ENABLE;
    }

    header = (struct apm_module_param_data_t *)payload;
    header->module_instance_id = miid;
    header->param_id =  PARAM_ID_SILENCE_DETECTION;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);

    silence_detection_cfg = (param_id_silence_detection_t *)(payload +
        sizeof(struct apm_module_param_data_t));
    silence_detection_cfg->enable_detection = 1;
    silence_detection_cfg->detection_duration_ms = rm->SilenceDetectionDuration();
    status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                 payload, payloadSize);
    if (status) {
        PAL_ERR(LOG_TAG, "Silence Detection enable param failed\n");
        builder->freeCustomPayload(&payload, &payloadSize);
        return -SD_ENABLE;
    }
    builder->freeCustomPayload(&payload, &payloadSize);
exit:
    return status;
}
