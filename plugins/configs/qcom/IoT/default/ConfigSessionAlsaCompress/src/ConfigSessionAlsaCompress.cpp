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

#define LOG_TAG "PAL: libsession_compress_config"

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
#include "PluginManagerIntf.h"
#include "ResourceManager.h"
#include "PayloadBuilder.h"
#include "SessionAR.h"
#include "SessionAlsaCompress.h"
#include "SessionAlsaUtils.h"
#include "ConfigSessionAlsaCompress.h"
#include "ConfigSessionUtils.h"
#include "ar_osal_mem_op.h"
#include "apm_api.h"
#include <agm/agm_api.h>
#include <gapless_api.h>

/*interface implementation*/
extern "C" int compressPluginConfig(Stream* stream, plugin_config_name_t config,
                 void *pluginPayload, size_t pluginPayloadSize)
{
    int status = 0;
    PAL_DBG(LOG_TAG, "Enter");
    switch(config) {
        case PAL_PLUGIN_CONFIG_START:
            status = compressPluginConfigSetConfigStart(stream, pluginPayload);
            break;
        case PAL_PLUGIN_CONFIG_POST_START:
            status = compressPluginConfigSetConfigPostStart(stream, pluginPayload);
            break;
        case PAL_PLUGIN_CONFIG_STOP:
            status = compressPluginConfigSetConfigStop(stream);
            break;
        case PAL_PLUGIN_PRE_RECONFIG:
            status = compressPluginPreReconfig(stream, pluginPayload);
            break;
        case PAL_PLUGIN_RECONFIG:
            status = reconfigCommon(stream, pluginPayload);
            break;
        case PAL_PLUGIN_CONFIG_SETPARAM:
            status = compressPluginConfigSetParam(stream, pluginPayload, pluginPayloadSize);
            break;
        default:
            PAL_ERR(LOG_TAG, "config type %d, is unsupported",config);
            status = -EINVAL;
    }
    PAL_DBG(LOG_TAG,"Exit status: %d", status);
    return status;
}

int32_t compressPluginPreReconfig(Stream* s, void* pluginPayload) {
    int status = 0;
    struct ReconfigPluginPayload* reconfigPld = nullptr;

    PAL_DBG(LOG_TAG,"Enter");

    if (!pluginPayload) {
        PAL_ERR(LOG_TAG, "plugin Payload is null");
        return -EINVAL;
    }
    reconfigPld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);

    if (!reconfigPld->config_ctrl.compare("silence_detection")) {
        status = compressSilenceDetectionConfig(SD_DISCONNECT, &reconfigPld->dAttr, pluginPayload);
    }
    PAL_DBG(LOG_TAG,"Exit status: %d", status);
    return status;
}

int32_t compressPluginConfigSetConfigPostStart(Stream* s, void* pluginPayload)
{
    int status = 0;
    pal_param_device_rotation_t rotation;
    std::shared_ptr<ResourceManager> rm = nullptr;
    struct mixer* mxr = nullptr;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<int> compressDevIds;
    PayloadBuilder* builder = new PayloadBuilder();
    SessionAlsaCompress* session = nullptr;

    PAL_DBG(LOG_TAG, "Enter");
    rm = ResourceManager::getInstance();
    session = reinterpret_cast<SessionAlsaCompress*>(pluginPayload);
    rxAifBackEnds = session->getRxBEVecRef();

    status = session->getFrontEndIds(compressDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "get mixer handle failed %d", status);
        goto exit;
    }

    rotation.rotation_type = rm->getOrientation() == ORIENTATION_270 ?
                            PAL_SPEAKER_ROTATION_RL : PAL_SPEAKER_ROTATION_LR;
    status = handleDeviceRotation(rm, s, rotation.rotation_type, compressDevIds.at(0), mxr,
                                                    builder, rxAifBackEnds);
    if (status != 0) {
        PAL_ERR(LOG_TAG,"handleDeviceRotation failed\n");
        goto exit;
    }
exit:
    if (builder) {
        delete builder;
        builder = nullptr;
    }
    PAL_DBG(LOG_TAG,"Exit status: %d", status);
    return status;
}
int32_t compressPluginConfigSetParam(Stream* s, void* pluginPayload, size_t ppldSize)
{
    int status = 0;
    int device = 0;
    uint32_t paramId = -1;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();
    struct mixer* mxr = nullptr;
    PayloadBuilder* builder = nullptr;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<int> compressDevIds;
    SetParamPluginPayload* ppld = reinterpret_cast<SetParamPluginPayload*>(pluginPayload);
    SessionAlsaCompress* session = static_cast<SessionAlsaCompress*>(ppld->session);
    paramId = ppld->paramId;

    PAL_DBG(LOG_TAG, "Enter, paramId: %d", paramId);

    switch (paramId) {
        case PAL_PARAM_ID_CODEC_CONFIGURATION:
        {
            pal_audio_fmt_t* audio_fmt = reinterpret_cast<pal_audio_fmt_t*>(ppld->payload);
            builder = reinterpret_cast<PayloadBuilder*>(ppld->builder);
            rxAifBackEnds = session->getRxBEVecRef();
            status = session->getFrontEndIds(compressDevIds);
            if (status) {
                PAL_ERR(LOG_TAG, "getFrontEndId(compressDevIds) failed %d", status);
                goto exit;
            }
            if (compressDevIds.size()) {
                device = compressDevIds.at(0);
            } else {
                PAL_ERR(LOG_TAG, "No compressDevIds found");
                status = -EINVAL;
                goto exit;
            }
            status = rm->getVirtualAudioMixer(&mxr);
            if (status) {
                PAL_ERR(LOG_TAG, "get mixer handle failed %d", status);
                goto exit;
            }
            status = setCustomFormatParam(*audio_fmt, builder, session, mxr, rxAifBackEnds, compressDevIds);
            if (status) {
                PAL_ERR(LOG_TAG, "setCustomFormatParam failed %d", status);
                goto exit;
            }
        }
        break;
        default:
            status = pluginConfigSetParam(s, ppld);
            break;
    }
exit:
    PAL_DBG(LOG_TAG,"Exit status: %d", status);
    return status;
}

int32_t compressPluginConfigSetConfigStart(Stream* s, void* pluginPayload)
{
    int32_t status = 0;
    pal_stream_attributes sAttr = {};
    pal_device dAttr = {};
    struct sessionToPayloadParam streamData = {};
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    std::vector<int> compressDevIds;
    Session* sess = nullptr;
    SessionAlsaCompress* session = nullptr;
    std::shared_ptr<ResourceManager> rm = nullptr;
    PayloadBuilder* builder = nullptr;
    struct mixer* mxr = nullptr;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    uint32_t miid = 0;
    PluginPayload* ppld = nullptr;
    pal_audio_fmt_t* audio_fmt = nullptr;

    PAL_DBG(LOG_TAG, "Enter");
    memset(&streamData, 0, sizeof(struct sessionToPayloadParam));
    memset(&dAttr, 0, sizeof(struct pal_device));

    status = s->getAssociatedSession(&sess);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "getAssociatedSession failed");
        goto exit;
    }
    session = static_cast<SessionAlsaCompress*>(sess);
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "stream get attributes failed");
        goto exit;
    }
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    status = s->getAssociatedDevices(associatedDevices);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedDevices Failed, status = %d \n", status);
        goto exit;
    }

    rxAifBackEnds = session->getRxBEVecRef();
    txAifBackEnds = session->getTxBEVecRef();
    rm->getBackEndNames(associatedDevices, rxAifBackEnds, txAifBackEnds);
    if (rxAifBackEnds.empty() && txAifBackEnds.empty()) {
        PAL_ERR(LOG_TAG, "no backend specified for this stream");
        goto exit;
    }
    ppld = reinterpret_cast<PluginPayload*>(pluginPayload);
    builder = reinterpret_cast<PayloadBuilder*>(ppld->builder);
    status = session->getFrontEndIds(compressDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }

    switch (sAttr.direction) {
        case PAL_AUDIO_OUTPUT:
            audio_fmt = reinterpret_cast<pal_audio_fmt_t*>(ppld->payload);
            if (!audio_fmt) {
                PAL_ERR(LOG_TAG, "audio_fmt is NULL!!!");
                goto exit;
            }
            setCustomFormatParam(*audio_fmt, builder, session, mxr, rxAifBackEnds, compressDevIds);
            for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                if(0 != status) {
                    PAL_ERR(LOG_TAG, "getAssociatedDevices Failed \n");
                    goto exit;
                }
                status = configureMFC(rm, sAttr, dAttr,
                                                compressDevIds,
                                                rxAifBackEnds[i].second.data(), builder);
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "build MFC payload failed");
                    goto exit;
                }

                if (session->getIsGaplessFmt()) {
                    status = configureEarlyEOSDelay(builder, mxr,
                                                    rxAifBackEnds, compressDevIds);
                }
                builder->getCustomPayload(&payload, &payloadSize);
                if (payload) {
                    status =
                        SessionAlsaUtils::setMixerParameter(mxr,
                                                    compressDevIds.at(0),
                                                    payload, payloadSize);
                    builder->freeCustomPayload();
                    if (status != 0) {
                        PAL_ERR(LOG_TAG, "setMixerParameter failed");
                        goto exit;
                    }
                }

                if (!status && session->getIsMixerEventCbRegd()
                            && !session->getIsPauseRegistrationDone()) {
                    // Register for callback for Soft Pause
                    size_t payload_size = 0;
                    struct agm_event_reg_cfg event_cfg;
                    payload_size = sizeof(struct agm_event_reg_cfg);
                    memset(&event_cfg, 0, sizeof(event_cfg));
                    event_cfg.event_id = EVENT_ID_SOFT_PAUSE_PAUSE_COMPLETE;
                    event_cfg.event_config_payload_size = 0;
                    event_cfg.is_register = 1;
                    status =
                        SessionAlsaUtils::registerMixerEvent(mxr,
                                                compressDevIds.at(0),
                                    rxAifBackEnds[0].second.data(),
                                    TAG_PAUSE, (void *)&event_cfg, payload_size);
                    if (status == 0) {
                        session->setIsPauseRegistrationDone(true);
                    } else {
                        // Not a fatal error
                        PAL_ERR(LOG_TAG, "Pause callback registration failed");
                        status = 0;
                    }
                }
                if ((rm->IsChargeConcurrencyEnabled()) &&
                    (dAttr.id == PAL_DEVICE_OUT_SPEAKER))
                {
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

                if (PAL_DEVICE_OUT_SPEAKER == dAttr.id
                        && !strcmp(dAttr.custom_config.custom_key, "mspp")) {

                    uint8_t* payload = nullptr;
                    size_t payloadSize = 0;
                    uint32_t miid;
                    int32_t volStatus;
                    volStatus =
                        SessionAlsaUtils::getModuleInstanceId(mxr,
                                                            compressDevIds.at(0),
                                                    rxAifBackEnds[0].second.data(),
                                                        TAG_MODULE_MSPP, &miid);
                    if (volStatus != 0) {
                        PAL_ERR(LOG_TAG,"get MSPP ModuleInstanceId failed");
                        break;
                    }

                    builder->payloadMSPPConfig(&payload, &payloadSize,
                                            miid, rm->getLinearGain().gain);
                    if (payloadSize && payload) {
                        volStatus = builder->updateCustomPayload(payload, payloadSize);
                        builder->freeCustomPayload(&payload, &payloadSize);
                        if (0 != volStatus) {
                            PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                            break;
                        }
                    }
                    builder->getCustomPayload(&payload, &payloadSize);
                    volStatus =
                        SessionAlsaUtils::setMixerParameter(mxr,
                                                        compressDevIds.at(0),
                                                        payload, payloadSize);
                    builder->freeCustomPayload();
                    if (volStatus != 0) {
                        PAL_ERR(LOG_TAG,"setMixerParameter failed for MSPP module");
                        break;
                    }

                    //to set soft pause delay for MSPP use case.
                    status = SessionAlsaUtils::getModuleInstanceId(mxr,
                                                        compressDevIds.at(0),
                                                        rxAifBackEnds[0].second.data(),
                                                        TAG_PAUSE, &miid);
                    if (status != 0) {
                        PAL_ERR(LOG_TAG,"get Soft Pause ModuleInstanceId failed");
                        break;
                    }

                    builder->payloadSoftPauseConfig(&payload, &payloadSize,
                                                    miid, MSPP_SOFT_PAUSE_DELAY);
                    if (payloadSize && payload) {
                        status = builder->updateCustomPayload(payload, payloadSize);
                        builder->freeCustomPayload(&payload, &payloadSize);
                        if (0 != status) {
                            PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                            break;
                        }
                    }
                    builder->getCustomPayload(&payload, &payloadSize);
                    status = SessionAlsaUtils::setMixerParameter(mxr, compressDevIds.at(0),
                                                                payload, payloadSize);
                    builder->freeCustomPayload();
                    if (status != 0) {
                        PAL_ERR(LOG_TAG,"setMixerParameter failed for soft Pause module");
                        break;
                    }
                }
            }
            break;
        case PAL_AUDIO_INPUT:
            status = SessionAlsaUtils::getModuleInstanceId(mxr, compressDevIds.at(0),
                                                        txAifBackEnds[0].second.data(),
                                                            TAG_STREAM_MFC_SR, &miid);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
                goto exit;
            }
            PAL_DBG(LOG_TAG, "miid : %x id = %d, data %s\n", miid,
                    compressDevIds.at(0), txAifBackEnds[0].second.data());

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
            if (payload) {
                status = SessionAlsaUtils::setMixerParameter(
                    mxr, compressDevIds.at(0), payload,
                    payloadSize);
                builder->freeCustomPayload();
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "setMixerParameter failed");
                    goto exit;
                }
            }

            for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                    goto exit;
                }
            }

            //Setting the device orientation during stream open for HDR record.
            if ((dAttr.id == PAL_DEVICE_IN_HANDSET_MIC
                    || dAttr.id == PAL_DEVICE_IN_SPEAKER_MIC)
                && strstr(dAttr.custom_config.custom_key, "unprocessed-hdr-mic"))
            {
                s->setOrientation(
                    session->HDRConfigKeyToDevOrientation(dAttr.custom_config.custom_key));
                PAL_DBG(LOG_TAG,"HDR record set device orientation %d", s->getOrientation());
                if (session->setConfig(s, MODULE, ORIENTATION_TAG) != 0)
                {
                    PAL_DBG(LOG_TAG,"HDR record setting device orientation failed");
                }
            }
            if (dAttr.id == PAL_DEVICE_IN_PROXY || dAttr.id == PAL_DEVICE_IN_RECORD_PROXY)
            {
                status = configureMFC(rm, sAttr, dAttr, compressDevIds,
                txAifBackEnds[0].second.data(), builder);
                if(status != 0) {
                    PAL_ERR(LOG_TAG, "build MFC payload failed");
                }
            }
            builder->getCustomPayload(&payload, &payloadSize);
            if (payload) {
                status = SessionAlsaUtils::setMixerParameter(
                    mxr, compressDevIds.at(0), payload,
                    payloadSize);
                builder->freeCustomPayload();
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "setMixerParameter failed");
                    goto exit;
                }
            }

            if (rm->IsSilenceDetectionEnabledPcm() &&
                       sAttr.type != PAL_STREAM_VOICE_CALL_RECORD) {
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
                    (void) enableSilenceDetection(rm, mxr, compressDevIds,
                              txAifBackEnds[0].second.data(), (uint64_t)session);
                              ppld->session = session;
                    ppld->builder = reinterpret_cast<void*>(builder);
                    status = compressSilenceDetectionConfig(SD_SETPARAM, nullptr, ppld);
                    if (status != 0) {
                        PAL_ERR(LOG_TAG, "Enable param failed for Silence Detection\n");
                        (void) disableSilenceDetection(rm, mxr, compressDevIds,
                                        txAifBackEnds[0].second.data(), (uint64_t)session);
                        goto silence_det_setup_done;
                    }
                }
silence_det_setup_done:
            status = 0;
            }
            break;
        default:
            break;
    }
exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

//moved from SessionAlsaCompress
int configureEarlyEOSDelay(PayloadBuilder* builder, struct mixer* mxr,
            std::vector<std::pair<int32_t, std::string>>& rxAifBackEnds,
                                        std::vector<int>& compressDevIds)
{
    int32_t status = 0;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    uint32_t miid = 0;

    PAL_DBG(LOG_TAG, "Enter");
    status = SessionAlsaUtils::getModuleInstanceId(mxr,
                    compressDevIds.at(0),
                    rxAifBackEnds[0].second.data(),
                    MODULE_GAPLESS, &miid);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
        return status;
    }
    param_id_gapless_early_eos_delay_t *early_eos_delay =
            new param_id_gapless_early_eos_delay_t;

    early_eos_delay->early_eos_delay_ms = EARLY_EOS_DELAY_MS;

    status = builder->payloadCustomParam(&payload, &payloadSize,
                                        (uint32_t *)early_eos_delay,
                    sizeof(struct param_id_gapless_early_eos_delay_t),
                                        miid, PARAM_ID_EARLY_EOS_DELAY);
    delete early_eos_delay;
    if (status) {
        PAL_ERR(LOG_TAG, "payloadCustomParam failed status = %d", status);
        return status;
    }
    if (payloadSize) {
        status = builder->updateCustomPayload(payload, payloadSize);
        builder->freeCustomPayload(&payload, &payloadSize);
        if(0 != status) {
            PAL_ERR(LOG_TAG, "%s: updateCustomPayload Failed\n", __func__);
            return status;
        }
    }
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int32_t compressPluginConfigSetConfigStop(Stream* s)
{
    int32_t status = 0;
    struct agm_event_reg_cfg event_cfg = {};
    struct pal_stream_attributes sAttr = {};
    std::shared_ptr<ResourceManager> rm = nullptr;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<int> compressDevIds;
    struct pal_device dAttr = {};
    Session* sess = nullptr;
    SessionAlsaCompress* session = nullptr;
    struct mixer* mxr = nullptr;
    uint8_t* payload = nullptr;
    size_t payload_size = 0;

    PAL_DBG(LOG_TAG, "Enter");
    status = s->getAssociatedSession(&sess);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "getAssociatedSession failed");
        goto exit;
    }
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "stream get attributes failed");
        goto exit;
    }
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }

    session = static_cast<SessionAlsaCompress*>(sess);
    rxAifBackEnds = session->getRxBEVecRef();
    txAifBackEnds = session->getTxBEVecRef();
    status = session->getFrontEndIds(compressDevIds);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }

    switch (sAttr.direction) {
        case PAL_AUDIO_OUTPUT:
            // Deregister for callback for Soft Pause
            if (session->getIsPauseRegistrationDone()) {
                payload_size = sizeof(struct agm_event_reg_cfg);
                memset(&event_cfg, 0, sizeof(event_cfg));
                event_cfg.event_id = EVENT_ID_SOFT_PAUSE_PAUSE_COMPLETE;
                event_cfg.event_config_payload_size = 0;
                event_cfg.is_register = 0;
                if (!compressDevIds.size()) {
                    PAL_ERR(LOG_TAG, "frontendIDs are not available");
                    status = -EINVAL;
                    goto exit;
                }
                if (!rxAifBackEnds.size()) {
                    PAL_ERR(LOG_TAG, "rxAifBackEnds are not available");
                    status = -EINVAL;
                    goto exit;
                }
                status = SessionAlsaUtils::registerMixerEvent(mxr, compressDevIds.at(0),
                            rxAifBackEnds[0].second.data(), TAG_PAUSE, (void *)&event_cfg,
                            payload_size);
                if (status == 0 || rm->getSoundCardState() == CARD_STATUS_OFFLINE) {
                    session->setIsPauseRegistrationDone(false);
                } else {
                    // Not a fatal error
                    PAL_ERR(LOG_TAG, "Pause callback deregistration failed\n");
                    status = 0;
                }
            }
            break;
        case PAL_AUDIO_INPUT:
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
                    (void) disableSilenceDetection(rm, mxr, compressDevIds,
                                 txAifBackEnds[0].second.data(), (uint64_t)session);
                }
silence_det_setup_done:
                status = 0;
            }
            break;
        case PAL_AUDIO_INPUT_OUTPUT:
            break;
    }
exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int setCustomFormatParam(pal_audio_fmt_t audio_fmt, PayloadBuilder* builder,
                        SessionAlsaCompress* session, struct mixer* mxr,
                        std::vector<std::pair<int32_t, std::string>>& rxAifBackEnds,
                        std::vector<int>& compressDevIds)
{
    int32_t status = 0;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    uint32_t miid = 0;
    struct media_format_t *media_fmt_hdr = nullptr;
    struct agm_buff buffer = {0, 0, 0, NULL, 0, NULL, {0, 0, 0}};
    bool sendNextTrackParams = false;
    struct snd_codec codec;

    PAL_DBG(LOG_TAG, "Enter");
    codec = session->getSndCodec();

    if (audio_fmt == PAL_AUDIO_FMT_VORBIS) {
        payload_media_fmt_vorbis_t* media_fmt_vorbis = NULL;
        // set config for vorbis, as it cannot be upstreamed.
        if (!compressDevIds.size()) {
            PAL_ERR(LOG_TAG, "No compressDevIds found");
            status = -EINVAL;
            return status;
        }
        status = SessionAlsaUtils::getModuleInstanceId(mxr,
                    compressDevIds.at(0), rxAifBackEnds[0].second.data(),
                    STREAM_INPUT_MEDIA_FORMAT, &miid);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
            return status;
        }
        media_fmt_hdr = (struct media_format_t *)
                            malloc(sizeof(struct media_format_t)
                                + sizeof(struct pal_snd_dec_vorbis));
        if (!media_fmt_hdr) {
            PAL_ERR(LOG_TAG, "failed to allocate memory");
            return -ENOMEM;
        }
        media_fmt_hdr->data_format = DATA_FORMAT_RAW_COMPRESSED ;
        media_fmt_hdr->fmt_id = MEDIA_FMT_ID_VORBIS;
        media_fmt_hdr->payload_size = sizeof(struct pal_snd_dec_vorbis);
        media_fmt_vorbis = (payload_media_fmt_vorbis_t*)(((uint8_t*)media_fmt_hdr) +
            sizeof(struct media_format_t));

        ar_mem_cpy(media_fmt_vorbis,
                            sizeof(struct pal_snd_dec_vorbis),
                            &codec.format,
                            sizeof(struct pal_snd_dec_vorbis));
        sendNextTrackParams = session->getSendNextTrackParams();
        if (sendNextTrackParams) {
            PAL_DBG(LOG_TAG, "sending next track param on datapath");
            buffer.timestamp = 0x0;
            buffer.flags = AGM_BUFF_FLAG_MEDIA_FORMAT;
            buffer.size = sizeof(struct media_format_t) +
                          sizeof(struct pal_snd_dec_vorbis);
            buffer.addr = (uint8_t *)media_fmt_hdr;
            payload = (uint8_t *)&buffer;
            payloadSize = sizeof(struct agm_buff);
            status = SessionAlsaUtils::mixerWriteDatapathParams(mxr,
                        compressDevIds.at(0), payload, payloadSize);
            free(media_fmt_hdr);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "mixerWriteWithMetadata failed %d", status);
                return status;
            }
            session->setSendNextTrackParams(false);
        } else {
            status = builder->payloadCustomParam(&payload, &payloadSize,
                                        (uint32_t *)media_fmt_hdr,
                                        sizeof(struct media_format_t) +
                                        sizeof(struct pal_snd_dec_vorbis),
                                        miid, PARAM_ID_MEDIA_FORMAT);
            free(media_fmt_hdr);
            if (status) {
                PAL_ERR(LOG_TAG, "payloadCustomParam failed status = %d", status);
                return status;
            }
            status = SessionAlsaUtils::setMixerParameter(mxr,
                            compressDevIds.at(0), payload, payloadSize);
            builder->freeCustomPayload(&payload, &payloadSize);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "setMixerParameter failed");
                return status;
            }
        }
    }
exit:
    PAL_DBG(LOG_TAG, "Exit status: %d", status);
    return status;
}

int compressSilenceDetectionConfig(uint8_t config, pal_device *dAttr,  void * pluginPayload) {
    int status = 0;
    uint32_t miid = 0;
    size_t pad_bytes = 0, payloadSize = 0;
    uint8_t* payload = NULL;
    struct apm_module_param_data_t* header = NULL;
    param_id_silence_detection_t *silence_detection_cfg = NULL;
    PluginPayload* ppld = nullptr;
    SessionAlsaCompress* session = nullptr;
    struct mixer* mxr = nullptr;
    std::vector<int> pcmDevIds;
    PayloadBuilder* builder = nullptr;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    std::shared_ptr<ResourceManager> rm = nullptr;

    ppld = reinterpret_cast<PluginPayload*>(pluginPayload);
    builder = reinterpret_cast<PayloadBuilder*>(ppld->builder);
    session = static_cast<SessionAlsaCompress*>(ppld->session);
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
