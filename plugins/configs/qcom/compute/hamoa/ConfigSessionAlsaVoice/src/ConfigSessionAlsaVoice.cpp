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
 */

/*
Changes from Qualcomm Technologies, Inc. are provided under the following license:
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#define LOG_TAG "PAL: libsession_voice_config"

#include <log/log.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <cutils/properties.h>
#include <kvh2xml.h>
#include "apm_api.h"
#include "PluginManagerIntf.h"
#include "ResourceManager.h"
#include "PayloadBuilder.h"
#include "SessionAR.h"
#include "SessionAlsaVoice.h"
#include "SessionAlsaUtils.h"
#include "ConfigSessionUtils.h"
#include "ConfigSessionAlsaVoice.h"

#define POP_SUPPRESSOR_RAMP_DELAY (1*1000)

/*interface implementation*/
extern "C" int voicePluginConfig(Stream* stream, plugin_config_name_t config,
                 void *pluginPayload, size_t ppldSize) {
    int status = 0;

    PAL_DBG(LOG_TAG,"Enter");
    switch(config) {
        case PAL_PLUGIN_CONFIG_START:
            status = voicePluginConfigSetConfigStart(stream, pluginPayload);
            break;
        case PAL_PLUGIN_CONFIG_POST_START:
            status = voicePluginConfigSetConfigPostStart(stream, pluginPayload);
            break;
        case PAL_PLUGIN_CONFIG_STOP:
            status = voicePluginConfigSetConfigStop(stream, pluginPayload);
            break;
        case PAL_PLUGIN_PRE_RECONFIG:
            status = voicePluginPreReconfig(stream, pluginPayload);
            break;
        case PAL_PLUGIN_RECONFIG:
            status = voicePluginReconfig(stream, pluginPayload);
            break;
        case PAL_PLUGIN_POST_RECONFIG:
            status = voicePostReconfig(stream, pluginPayload);
            break;
        default:
            PAL_ERR(LOG_TAG, "config type %d, is unsupported", config);
            status = -EINVAL;
    }
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

/**
 * Part of start logics in voice plugin.
 */
int configVSID(Stream *s, SessionAlsaVoice *session, configType type __unused, uint32_t vsid, int dir, PayloadBuilder* builder)
{
    int status = 0;
    uint8_t* customPayload = nullptr;
    size_t customPayloadSize = 0;
    struct mixer* mxr = nullptr;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

    status = payloadSetVSID(s, builder, vsid);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "failed to get payload status %d", status);
        goto exit;
    }
    builder->getCustomPayload(&customPayload, &customPayloadSize);
    PAL_DBG(LOG_TAG, "customPayload: %d customPayloadSize: %d", customPayload, customPayloadSize);
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    status = session->setVoiceMixerParameter(s, mxr,
                                                customPayload,
                                                customPayloadSize,
                                                dir);
    if (status) {
        PAL_ERR(LOG_TAG, "Failed to set voice params status = %d",
                status);
        goto exit;
    }
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}
int32_t voicePostCommonReconfig(Stream* s, void* pluginPayload)
{
    int status = 0;
    uint32_t vsid = 0;
    struct pal_stream_attributes sAttr;
    Session* sess = nullptr;
    SessionAlsaVoice* session = nullptr;
    PayloadBuilder* builder = new PayloadBuilder();
    struct ReconfigPluginPayload* reconfigPld = nullptr;
    std::vector<std::pair<int32_t, std::string>> aifBackEndsToConnect;

    PAL_DBG(LOG_TAG,"Enter");
    status = s->getAssociatedSession(&sess);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedSession Failed \n");
        goto exit;
    }
    session = static_cast<SessionAlsaVoice*>(sess);
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        PAL_ERR(LOG_TAG,"stream get attributes failed");
        goto exit;
    }
    reconfigPld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);
    aifBackEndsToConnect = reconfigPld->aifBackEnds;
    if (session)
        vsid = session->getVSID();

    if (!(SessionAlsaUtils::isMmapUsecase(sAttr))) {
        if (session) {
            if (s->getCurState() != STREAM_INIT) {
                if (SessionAlsaUtils::isRxDevice(aifBackEndsToConnect[0].first)) {
                    reconfigureSession(s, builder, vsid, PAL_AUDIO_OUTPUT);
                } else {
                    reconfigureSession(s, builder, vsid, PAL_AUDIO_INPUT);
                }
            }
        } else {
            PAL_ERR(LOG_TAG, "invalid session voice object");
            status = -EINVAL;
            goto exit;
        }
    }
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int32_t voicePreCommonReconfig(Stream* s)
{
    int status = 0;
    Session* sess = nullptr;
    SessionAlsaVoice* session = nullptr;
    std::shared_ptr<ResourceManager> rm = nullptr;
    std::vector<int> pcmDevRxIds;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    pal_device dAttr = {};

    PAL_DBG(LOG_TAG,"Enter");
    status = s->getAssociatedSession(&sess);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedSession Failed \n");
        goto exit;
    }
    session = static_cast<SessionAlsaVoice*>(sess);
    rxAifBackEnds = session->getRxBEVecRef();
    status = session->getFrontEndIds(pcmDevRxIds, RX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevRxIds) failed %d", status);
        goto exit;
    }
    if (rxAifBackEnds.size() > 0) {
        //non-fatal call.
        setTaggedSlotMask(s, rm, pcmDevRxIds);
        PAL_INFO(LOG_TAG,"setTaggedSlotMask() returned; non-fatal call.");
    }

exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int32_t voicePostReconfig(Stream* s, void* pluginPayload) {
    int status = 0;
    struct ReconfigPluginPayload* reconfigPld = nullptr;
    reconfigPld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);
    std::shared_ptr<ResourceManager> rm = nullptr;

    PAL_DBG(LOG_TAG,"Enter");
    rm = ResourceManager::getInstance();
    if (!reconfigPld->config_ctrl.compare("silence_detection")) {
        status = voiceSilenceDetectionConfig(SD_CONNECT, &reconfigPld->dAttr, pluginPayload);
        if (status) {
            goto exit;
        }
    } else if (rm->IsCRSCallEnabled()) {
        status = rxMFCCoeffConfig(s, pluginPayload);
    }

exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int32_t voicePluginPreReconfig(Stream* s, void* pluginPayload) {
    int status = 0;
    struct ReconfigPluginPayload* reconfigPld = nullptr;
    reconfigPld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);

    PAL_DBG(LOG_TAG,"Enter");
    if (!reconfigPld->config_ctrl.compare("silence_detection")) {
        status = voiceSilenceDetectionConfig(SD_DISCONNECT, &reconfigPld->dAttr, pluginPayload);
    } else {
        /*config mute on pop suppressor*/
        setPopSuppressorMute(s);
        usleep(POP_SUPPRESSOR_RAMP_DELAY);
    }

    PAL_DBG(LOG_TAG,"Exit");
    return status;
}
/*
 * Leaving the above and the bottom APIs in case more module specific logics
 * from SessionAlsaVoice need to be moved here in voice plugin.
 */
int32_t voicePluginConfigSetConfigPostStart(Stream* s, void* pluginPayload) {
    int status = 0;
    std::shared_ptr<ResourceManager> rm = nullptr;

    PAL_DBG(LOG_TAG,"Enter");
    rm = ResourceManager::getInstance();
    if (rm->IsCRSCallEnabled()) {
        status = rxMFCCoeffConfig(s, pluginPayload);
    }
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int32_t rxMFCCoeffConfig(Stream* s, void* pluginPayload) {
    int status = 0;
    ReconfigPluginPayload* ppld = nullptr;
    SessionAlsaVoice* session = nullptr;
    std::shared_ptr<Device>* rxDevice = nullptr;
    PayloadBuilder* builder = nullptr;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();
    std::vector<int> pcmDevRxIds;

    PAL_DBG(LOG_TAG,"Enter");
    ppld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);
    session = static_cast<SessionAlsaVoice*>(ppld->session);
    builder = reinterpret_cast<PayloadBuilder*>(ppld->builder);
    rxDevice = reinterpret_cast<std::shared_ptr<Device>*>(ppld->payload);
    pcmDevRxIds = ppld->pcmDevIds;
    if (!session || !builder || !rxDevice || pcmDevRxIds.empty()) {
        status = -EINVAL;
        PAL_ERR(LOG_TAG, "nullptr detected in one of utilities!!!");
        goto exit;
    }
    status = populate_rx_mfc_coeff_payload(*rxDevice, session, builder, pcmDevRxIds, rm);
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

/**
 * Logic originally in SessionAlsaVoice::start(); after pcm_open()s, before pcm_start()s
 * for rx and tx paths. Module-specific logic moved here. e.g. configVSID.
 */
int32_t voicePluginConfigSetConfigStart(Stream* s, void* pluginPayload)
{
    int status = 0;
    uint32_t vsid = 0;
    struct mixer* mxr = nullptr;
    Session* sess = nullptr;
    SessionAlsaVoice* session = nullptr;
    PayloadBuilder* builder = nullptr;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    std::vector<int> pcmDevRxIds;
    std::vector<int> pcmDevIds;
    std::shared_ptr<ResourceManager> rm = nullptr;
    struct pal_device dAttr = {};
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    ReconfigPluginPayload ppld;

    PAL_DBG(LOG_TAG,"Enter");
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    status = s->getAssociatedSession(&sess);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedSession Failed \n");
        goto exit;
    }
    session = static_cast<SessionAlsaVoice*>(sess);
    builder = new PayloadBuilder();
    vsid = *(reinterpret_cast<int*>(pluginPayload));

    status = session->getFrontEndIds(pcmDevIds, TX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }

    status = configVSID(s, session, MODULE, vsid, RX_HOSTLESS, builder);
    if (status) {
        PAL_ERR(LOG_TAG, "setConfig failed %d", status);
        goto exit;
    }

    session->setConfig(s, MODULE, CHANNEL_INFO, TX_HOSTLESS);

    /* configuring RAT_RENDER, updating custom payload if it is a NB/WB SCO usecase*/
    status = populateRatPayload(s, session, builder);
    if (status != 0) {
        PAL_ERR(LOG_TAG,"Exit Configuring RAT_RENDER failed with status %d", status);
        goto exit;
    }

    /* configuring Rx MFC's, updating custom payload and send mixer controls at once*/
    status = build_rx_mfc_payload(s, builder);
    if (status != 0) {
        PAL_ERR(LOG_TAG,"Exit Configuring Rx mfc failed with status %d", status);
        goto exit;
    }
    status = session->getFrontEndIds(pcmDevRxIds, RX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevRxIds) failed %d", status);
        goto exit;
    }
    builder->getCustomPayload(&payload, &payloadSize);
    status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevRxIds.at(0),
                                                 payload, payloadSize);
    builder->freeCustomPayload();
    if (status != 0) {
        PAL_ERR(LOG_TAG,"setMixerParameter failed");
        goto exit;
    }
    /* set slot_mask as TKV to configure MUX module */
    status = setTaggedSlotMask(s, rm, pcmDevRxIds);
    if (status != 0) {
        PAL_ERR(LOG_TAG,"setTaggedSlotMask failed");
        goto exit;
    }

    if (rm->IsSilenceDetectionEnabledVoice()) {

        status = s->getAssociatedDevices(associatedDevices);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "getAssociatedDevices Failed for Silence Detection\n");
            goto silence_det_setup_done;
        }

        for (int i=0; i<associatedDevices.size(); i++) {
        status = associatedDevices[i]->getDeviceAttributes(&dAttr);
            if (0 != status) {
                PAL_ERR(LOG_TAG, "getDeviceAttributes Failed for Silence Detection\n");
                goto silence_det_setup_done;
            }
        }

        if ((dAttr.id != PAL_DEVICE_IN_HANDSET_MIC) && (dAttr.id != PAL_DEVICE_IN_SPEAKER_MIC))
            goto silence_det_setup_done;

        txAifBackEnds = session->getTxBEVecRef();
        (void) enableSilenceDetection(rm, mxr, pcmDevIds,
                        txAifBackEnds[0].second.data(), (uint64_t)session);
                        ppld.session = session;
        ppld.builder = reinterpret_cast<void*>(builder);
        status = voiceSilenceDetectionConfig(SD_SETPARAM, nullptr, &ppld);
        if (status != 0) {
             PAL_ERR(LOG_TAG, "Enable Param Failed for Silence Detection\n");
             (void) disableSilenceDetection(rm, mxr, pcmDevIds,
                             txAifBackEnds[0].second.data(), (uint64_t)session);
        }

silence_det_setup_done:
        status = 0;
    }
exit:
    if (builder)
        delete builder;
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

/**
 * Logic originally in SessionAlsaVoice::stop().
 * Module-specific logic moved here. e.g. pop noise suppressor
 */
int32_t voicePluginConfigSetConfigStop(Stream *s, void* pluginPayload)
{
    std::vector<std::shared_ptr<Device>> associatedDevices;
    struct pal_device dAttr = {};
    std::vector<int> pcmDevIds;
    std::shared_ptr<ResourceManager> rm = nullptr;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    SessionAlsaVoice* session = nullptr;
    struct mixer* mxr = nullptr;
    ReconfigPluginPayload *ppld;
    int status = 0;

    PAL_DBG(LOG_TAG,"Enter");

    ppld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);
    session = static_cast<SessionAlsaVoice*>(ppld->session);
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }

    if (rm->IsSilenceDetectionEnabledVoice()) {
        txAifBackEnds = session->getTxBEVecRef();
        status = session->getFrontEndIds(pcmDevIds, TX_HOSTLESS);
        if (status) {
            PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
            goto exit;
        }

        status = s->getAssociatedDevices(associatedDevices);
        if (0 != status) {
           PAL_ERR(LOG_TAG,"getAssociatedDevices Failed\n");
           goto silence_det_setup_done;
        }

        for (int i = 0; i < associatedDevices.size();i++) {
            status = associatedDevices[i]->getDeviceAttributes(&dAttr);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"get Device Attributes Failed\n");
                goto silence_det_setup_done;
            }
        }
        if (dAttr.id == PAL_DEVICE_IN_HANDSET_MIC || dAttr.id ==  PAL_DEVICE_IN_SPEAKER_MIC) {
            (void) disableSilenceDetection(rm, mxr,
                            pcmDevIds, txAifBackEnds[0].second.data(), (uint64_t)session);
        }

silence_det_setup_done:
        status = 0;
    }
    /*config mute on pop suppressor*/
    setPopSuppressorMute(s);
    usleep(POP_SUPPRESSOR_RAMP_DELAY);
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int build_rx_mfc_payload(Stream *s, PayloadBuilder* builder)
{
    int status = 0;
    std::vector<uint32_t> rx_mfc_tags{PER_STREAM_PER_DEVICE_MFC,
                                    TAG_DEVICE_PP_MFC, TAG_MFC_SIDETONE};

    PAL_DBG(LOG_TAG,"Enter");
    for (uint32_t rx_mfc_tag : rx_mfc_tags) {
        status = populate_rx_mfc_payload(s, rx_mfc_tag, builder);
        if (status != 0) {
            PAL_ERR(LOG_TAG,"populating Rx mfc: %X payload failed :%d",
                    rx_mfc_tag, status);
            if (rx_mfc_tag == TAG_DEVICE_PP_MFC)
                goto exit;
        }
    }
    status = 0;

exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int populate_rx_mfc_payload(Stream *s, uint32_t rx_mfc_tag, PayloadBuilder* builder)
{
    int status = 0;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    std::vector<int> pcmDevRxIds;
    std::vector<int> pcmDevTxIds;
    struct sessionToPayloadParam deviceData;
    struct mixer* mxr = nullptr;
    uint8_t* payload = nullptr;
    Session* sess = nullptr;
    SessionAlsaVoice* session = nullptr;
    size_t payloadSize = 0;
    uint32_t miid = 0;
    std::shared_ptr<ResourceManager> rm = nullptr;

    PAL_DBG(LOG_TAG,"Enter");
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    status = s->getAssociatedDevices(associatedDevices);
    if ((0 != status) || (associatedDevices.size() == 0)) {
        PAL_ERR(LOG_TAG, "getAssociatedDevices fails or empty associated devices");
        goto exit;
    }
    status = s->getAssociatedSession(&sess);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedSession Failed \n");
        goto exit;
    }
    session = static_cast<SessionAlsaVoice*>(sess);
    rxAifBackEnds = session->getRxBEVecRef();
    txAifBackEnds = session->getTxBEVecRef();

    rm->getBackEndNames(associatedDevices, rxAifBackEnds, txAifBackEnds);
    if (rxAifBackEnds.empty() && txAifBackEnds.empty()) {
        status = -EINVAL;
        PAL_ERR(LOG_TAG, "no backend specified for this stream");
        return status;
    }
    status = session->getFrontEndIds(pcmDevRxIds, RX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevRxIds) failed %d", status);
        goto exit;
    }
    status = session->getFrontEndIds(pcmDevTxIds, TX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevTxIds) failed %d", status);
        goto exit;
    }

    // For MIID of the MFC in the sidetone graph
    if (rx_mfc_tag == TAG_MFC_SIDETONE && pcmDevTxIds.size() > 0 && txAifBackEnds.size() > 0) {
        status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevTxIds.at(0),
                                                txAifBackEnds[0].second.c_str(),
                                                            rx_mfc_tag, &miid);
    }
    else if (pcmDevRxIds.size() > 0 && rxAifBackEnds.size() > 0) {
        status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevRxIds.at(0),
                                                rxAifBackEnds[0].second.c_str(),
                                                            rx_mfc_tag, &miid);
    }
    else {
        PAL_ERR(LOG_TAG, "Empty FE or BE!!!");
    }
    if (status != 0) {
        PAL_ERR(LOG_TAG,"getModuleInstanceId failed for Rx mfc: %X status: %d",
            rx_mfc_tag, status);
        return status;
    }
    status = getDeviceData(s, &deviceData);
    if(status){
        PAL_ERR(LOG_TAG,"failed to get deviceData")
        goto exit;
    }
    builder->payloadMFCConfig(&payload, &payloadSize, miid, &deviceData);
    if (payload && payloadSize) {
        status = builder->updateCustomPayload(payload, payloadSize);
        builder->freeCustomPayload(&payload, &payloadSize);
        if (status != 0)
            PAL_ERR(LOG_TAG,"updateCustomPayload for Rx mfc %XFailed\n", rx_mfc_tag);
    }
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int getDeviceData(Stream *s, struct sessionToPayloadParam *deviceData)
{
    int status = 0;
    int dev_id = 0;
    struct pal_device dAttr = {};
    int idx = 0;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::shared_ptr<ResourceManager> rm;
    rm = ResourceManager::getInstance();

    PAL_DBG(LOG_TAG,"Enter");
    if(!s){
        PAL_ERR(LOG_TAG, "invalid stream pointer")
        status = -EINVAL;
        goto exit;
    }
    if(!deviceData){
        PAL_ERR(LOG_TAG, "invalid deviceData pointer")
        status = -EINVAL;
        goto exit;
    }

    memset(&dAttr, 0, sizeof(struct pal_device));
    status = s->getAssociatedDevices(associatedDevices);
    if ((0 != status) || (associatedDevices.size() == 0)) {
        PAL_ERR(LOG_TAG, "getAssociatedDevices fails or empty associated devices");
        goto exit;
    }
    for (idx = 0; idx < associatedDevices.size(); idx++) {
        dev_id = associatedDevices[idx]->getSndDeviceId();
        if (rm->isOutputDevId(dev_id)) {
            status = associatedDevices[idx]->getDeviceAttributes(&dAttr);
            break;
        }
    }
    if (dAttr.id == 0) {
        PAL_ERR(LOG_TAG, "Failed to get device attributes");
        status = -EINVAL;
        goto exit;
    }

    if (dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_SCO || dAttr.id == PAL_DEVICE_OUT_BLUETOOTH_BLE)
    {
        struct pal_media_config codecConfig;
        status = associatedDevices[idx]->getCodecConfig(&codecConfig);
        if(0 != status) {
           PAL_ERR(LOG_TAG,"getCodecConfig Failed \n");
            goto exit;
        }
        deviceData->bitWidth = codecConfig.bit_width;
        deviceData->sampleRate = codecConfig.sample_rate;
        deviceData->numChannel = codecConfig.ch_info.channels;
        deviceData->ch_info = nullptr;
        PAL_DBG(LOG_TAG,"set devicePPMFC to match codec configuration for device %d\n", dAttr.id);
    } else {
        // update device pp configuration if virtual port is enabled
        if (rm->getActiveGroupDevConfig() &&
            (dAttr.id == PAL_DEVICE_OUT_SPEAKER ||
             dAttr.id == PAL_DEVICE_OUT_HANDSET)) {
            if (rm->getActiveGroupDevConfig()->devpp_mfc_cfg.sample_rate)
                dAttr.config.sample_rate = rm->getActiveGroupDevConfig()->devpp_mfc_cfg.sample_rate;
            if (rm->getActiveGroupDevConfig()->devpp_mfc_cfg.channels)
                dAttr.config.ch_info.channels = rm->getActiveGroupDevConfig()->devpp_mfc_cfg.channels;
        }
        deviceData->bitWidth = dAttr.config.bit_width;
        deviceData->sampleRate = dAttr.config.sample_rate;
        deviceData->numChannel = dAttr.config.ch_info.channels;
        deviceData->ch_info = nullptr;
    }
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int setPopSuppressorMute(Stream *s)
{
    int status = 0;
    std::shared_ptr<ResourceManager> rm = nullptr;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    std::vector<int> pcmDevRxIds;
    std::vector<int> pcmDevTxIds;
    Session* sess = nullptr;
    SessionAlsaVoice* session = nullptr;
    PayloadBuilder* builder = nullptr;
    struct mixer* mxr = nullptr;
    uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    uint32_t miid = 0;

    PAL_DBG(LOG_TAG,"Enter");
    status = s->getAssociatedSession(&sess);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedSession Failed \n");
        goto exit;
    }
    session = static_cast<SessionAlsaVoice*>(sess);

    status = s->getAssociatedDevices(associatedDevices);
    if ((0 != status) || (associatedDevices.size() == 0)) {
        PAL_ERR(LOG_TAG, "getAssociatedDevices fails or empty associated devices");
        goto exit;
    }
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    status = session->getFrontEndIds(pcmDevRxIds, RX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevRxIds) failed %d", status);
        goto exit;
    }
    rxAifBackEnds = session->getRxBEVecRef();
    if (!rxAifBackEnds.size()) {
        PAL_ERR(LOG_TAG, "No RX backends found");
        status = -EINVAL;
        goto exit;
    }
    txAifBackEnds = session->getTxBEVecRef();
    if (!pcmDevRxIds.size()) {
        PAL_ERR(LOG_TAG, "No pcmDevRxIds found");
        status = -EINVAL;
        goto exit;
    }
    rm = ResourceManager::getInstance();
    builder = new PayloadBuilder();
    status = SessionAlsaUtils::getModuleInstanceId(mxr, pcmDevRxIds.at(0),
                                            rxAifBackEnds[0].second.c_str(),
                                            DEVICE_POP_SUPPRESSOR, &miid);
    if (status != 0) {
        PAL_ERR(LOG_TAG,"getModuleInstanceId failed for Rx pop suppressor: 0x%x status: %d",
            DEVICE_POP_SUPPRESSOR, status);
        goto exit;
    }
    if (!builder) {
        PAL_ERR(LOG_TAG,"failed: builder instance not found");
        status = -EINVAL;
        goto exit;
    }
    status = builder->payloadPopSuppressorConfig((uint8_t**)&payload,
                                                        &payloadSize,
                                                        miid, true);
    if (status) {
        PAL_ERR(LOG_TAG,"pop suppressor payload creation failed: status: %d",
                status);
        goto exit;
    }
    status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevRxIds.at(0),
                                                 payload, payloadSize);
    if (status) {
        PAL_ERR(LOG_TAG,"setMixerParameter failed");
    }
exit:
    if (builder)
        delete builder;
    if (payload)
        free(payload);
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int setTaggedSlotMask(Stream* s, std::shared_ptr<ResourceManager> rm, std::vector<int> pcmDevRxIds)
{
    int status = 0;
    struct pal_device dAttr;
    struct pal_stream_attributes sAttr;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    int dev_id = 0;
    int idx = 0;

    PAL_DBG(LOG_TAG,"Enter");
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        PAL_ERR(LOG_TAG,"stream get attributes failed");
        return status;
    }

    memset(&dAttr, 0, sizeof(struct pal_device));
    status = s->getAssociatedDevices(associatedDevices);
    if ((0 != status) || (associatedDevices.size() == 0)) {
        PAL_ERR(LOG_TAG, "getAssociatedDevices fails or empty associated devices");
        return status;
    }

    for (idx = 0; idx < associatedDevices.size(); idx++) {
        dev_id = associatedDevices[idx]->getSndDeviceId();
        if (rm->isOutputDevId(dev_id)) {
            status = associatedDevices[idx]->getDeviceAttributes(&dAttr);
            break;
        }
    }
    if (dAttr.id == 0) {
        PAL_ERR(LOG_TAG, "Failed to get device attributes");
        status = -EINVAL;
        return status;
    }

    if ((rm->IsDeviceMuxConfigEnabled() || rm->IsVirtualPortForUPDEnabled()) &&
        (dAttr.id == PAL_DEVICE_OUT_SPEAKER ||dAttr.id == PAL_DEVICE_OUT_HANDSET)) {
        setSlotMask(rm, sAttr, dAttr, pcmDevRxIds); //statically linked in ConfigSessionUtils
    }

    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int reconfigureSession(Stream* s, PayloadBuilder* builder, uint32_t vsid, pal_stream_direction_t dir)
{
    int status = 0;
    int pcmId = 0;
    uint8_t* payload = nullptr;
    std::shared_ptr<ResourceManager> rm = nullptr;
    Session* sess = nullptr;
    SessionAlsaVoice* session = nullptr;
    size_t payloadSize = 0;
    struct mixer* mxr;
    std::vector<int> pcmDevRxIds;
    std::vector<int> pcmDevTxIds;

    PAL_DBG(LOG_TAG,"Enter");
    if (!dir)
        return -EINVAL;

    status = s->getAssociatedSession(&sess);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedSession Failed \n");
        goto exit;
    }
    session = static_cast<SessionAlsaVoice*>(sess);
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    status = session->getFrontEndIds(pcmDevRxIds, RX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevRxIds) failed %d", status);
        goto exit;
    }
    status = session->getFrontEndIds(pcmDevTxIds, TX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds(pcmDevTxIds) failed %d", status);
        goto exit;
    }

    if (dir == PAL_AUDIO_OUTPUT) {
        if (pcmDevRxIds.size())
            pcmId = pcmDevRxIds.at(0);
        status = build_rx_mfc_payload(s, builder);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"populating Rx mfc payload failed :%d", status);
            goto exit;
        }

        // populate_vsid_payload, appends to the existing payload
        status = populate_vsid_payload(s, builder, vsid);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"populating vsid payload for RX Failed:%d", status);
            goto exit;
        }

        // set tagged slot mask
        status = setTaggedSlotMask(s, rm, pcmDevRxIds);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"setTaggedSlotMask failed:%d", status);
            goto exit;
        }

        status = populateRatPayload(s, session, builder);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"populateRatPayload failed:%d", status);
            goto exit;
        }
    } else {
        if (pcmDevTxIds.size()) {
            pcmId = pcmDevTxIds.at(0);
        } else {
            PAL_ERR(LOG_TAG, "pcmDevTxIds is not available.");
            status = -EINVAL;
            goto exit;
        }
        status = populate_vsid_payload(s, builder, vsid);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"populating vsid payload for TX Failed:%d", status);
            goto exit;
        }
        status = populate_ch_info_payload(s, builder, vsid);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"populating channel info for TX Failed..skipping:%d", status);
        }
    }

    builder->getCustomPayload(&payload, &payloadSize);
    status = SessionAlsaUtils::setMixerParameter(mxr, pcmId,
            payload, payloadSize);

    if (status != 0) {
        PAL_ERR(LOG_TAG,"setMixerParameter failed:%d for dir:%s",
                status, (dir == RX_HOSTLESS)?"RX":"TX");
        goto exit;
    }

exit:
    builder->freeCustomPayload();
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int populate_vsid_payload(Stream *s, PayloadBuilder* builder, uint32_t vsid)
{
    int status = 0;
    size_t vsidpayloadSize = 0, padBytes = 0;
    vcpm_param_vsid_payload_t vsid_payload;
    apm_module_param_data_t* header = nullptr;
    uint8_t* vsidPayload = nullptr;
    uint8_t *vsid_pl = nullptr;
    Session* sess = nullptr;
    SessionAlsaVoice* session = nullptr;

    PAL_DBG(LOG_TAG,"Enter");
    status = s->getAssociatedSession(&sess);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedSession Failed \n");
        return status;
    }
    session = static_cast<SessionAlsaVoice*>(sess);
    vsidpayloadSize = sizeof(struct apm_module_param_data_t) +
                  sizeof(vcpm_param_vsid_payload_t);
    padBytes = PAL_PADDING_8BYTE_ALIGN(vsidpayloadSize);

    vsidPayload = (uint8_t *) calloc(1, vsidpayloadSize + padBytes);
    if (!vsidPayload) {
        PAL_ERR(LOG_TAG, "vsid payload allocation failed %s", strerror(errno));
        return -EINVAL;
    }

    header = (apm_module_param_data_t*)vsidPayload;
    header->module_instance_id = VCPM_MODULE_INSTANCE_ID;
    header->param_id = VCPM_PARAM_ID_VSID;
    header->error_code = 0x0;
    header->param_size = vsidpayloadSize - sizeof(struct apm_module_param_data_t);

    vsid_payload.vsid = vsid;
    vsid_pl = (uint8_t*)vsidPayload + sizeof(apm_module_param_data_t);
    ar_mem_cpy(vsid_pl,  sizeof(vcpm_param_vsid_payload_t),
                     &vsid_payload,  sizeof(vcpm_param_vsid_payload_t));
    vsidpayloadSize += padBytes;

    if (vsidPayload && vsidpayloadSize) {
        status = builder->updateCustomPayload(vsidPayload, vsidpayloadSize);
        builder->freeCustomPayload(&vsidPayload, &vsidpayloadSize);
        if (status != 0) {
            PAL_ERR(LOG_TAG,"updateCustomPayload for vsid payload Failed %d\n", status);
            return status;
        }
    }

    /* call loopback delay playload if in loopback mode*/
    if ((vsid == VOICELBMMODE1 || vsid == VOICELBMMODE2)) {
        populateVSIDLoopbackPayload(s, builder, vsid);
    }

    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

/**
 * Part of configVSID() logics.
 */
int payloadSetVSID(Stream* s, PayloadBuilder* builder, uint32_t vsid)
{
    int status = 0;
    apm_module_param_data_t* header;
    uint8_t* payloadInfo = nullptr;
    size_t payloadSize = 0, padBytes = 0;
    uint8_t *vsid_pl;
    vcpm_param_vsid_payload_t vsid_payload;

    PAL_DBG(LOG_TAG,"Enter");
    payloadSize = sizeof(struct apm_module_param_data_t)+
                  sizeof(vcpm_param_vsid_payload_t);
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = (uint8_t *) calloc(1, payloadSize + padBytes);
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
        return -EINVAL;
    }
    header = (apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = VCPM_MODULE_INSTANCE_ID;
    header->param_id = VCPM_PARAM_ID_VSID;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);

    vsid_payload.vsid = vsid;
    PAL_DBG(LOG_TAG, "vsid: %d", vsid_payload.vsid);
    vsid_pl = (uint8_t*)payloadInfo + sizeof(apm_module_param_data_t);
    ar_mem_cpy(vsid_pl,  sizeof(vcpm_param_vsid_payload_t),
                     &vsid_payload,  sizeof(vcpm_param_vsid_payload_t));
    payloadSize += padBytes;

    if (payloadInfo && payloadSize) {
        status = builder->updateCustomPayload(payloadInfo, payloadSize);
        builder->freeCustomPayload(&payloadInfo, &payloadSize);
        if (status != 0) {
            PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
            return status;
        }
    }
    /* call loopback delay playload if in loopback mode*/
    if ((vsid == VOICELBMMODE1 || vsid == VOICELBMMODE2)) {
        populateVSIDLoopbackPayload(s, builder, vsid);
    }
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int populateVSIDLoopbackPayload(Stream* s, PayloadBuilder* builder, uint32_t vsid)
{
    int status = 0;
    struct vsid_info vsidInfo;
    apm_module_param_data_t* header;
    uint8_t* loopbackPayload = nullptr;
    size_t loopbackPayloadSize = 0, padBytes = 0;
    uint8_t *loopback_pl;
    vcpm_param_id_voc_pkt_loopback_delay_t vsid_loopback_payload;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

    PAL_DBG(LOG_TAG,"Enter");
    rm->getVsidInfo(&vsidInfo);

    PAL_DBG(LOG_TAG, "loopback delay is %d", vsidInfo.loopback_delay);

    if (vsidInfo.loopback_delay == 0) {
        goto exit;
    }

    loopbackPayloadSize = sizeof(struct apm_module_param_data_t)+
                  sizeof(vcpm_param_id_voc_pkt_loopback_delay_t);
    padBytes = PAL_PADDING_8BYTE_ALIGN(loopbackPayloadSize);

    loopbackPayload = (uint8_t *) calloc(1, loopbackPayloadSize + padBytes);
    if (!loopbackPayload) {
        PAL_ERR(LOG_TAG, "loopback payload allocation failed %s",
            strerror(errno));
        return -EINVAL;
    }
    header = (apm_module_param_data_t*)loopbackPayload;
    header->module_instance_id = VCPM_MODULE_INSTANCE_ID;
    header->param_id = VCPM_PARAM_ID_VOC_PKT_LOOPBACK_DELAY;
    header->error_code = 0x0;
    header->param_size = loopbackPayloadSize - sizeof(struct apm_module_param_data_t);

    vsid_loopback_payload.vsid = vsid;
    vsid_loopback_payload.delay_ms = vsidInfo.loopback_delay;
    loopback_pl = (uint8_t*)loopbackPayload + sizeof(apm_module_param_data_t);
    ar_mem_cpy(loopback_pl,  sizeof(vcpm_param_id_voc_pkt_loopback_delay_t),
                     &vsid_loopback_payload,  sizeof(vcpm_param_id_voc_pkt_loopback_delay_t));

    loopbackPayloadSize += padBytes;

    if (loopbackPayload && loopbackPayloadSize) {
        status = builder->updateCustomPayload(loopbackPayload, loopbackPayloadSize);
        builder->freeCustomPayload(&loopbackPayload, &loopbackPayloadSize);
        if (status != 0) {
            PAL_ERR(LOG_TAG,"updateCustomPayload for loopback payload Failed %d\n", status);
            return status;
        }
    }
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int populate_ch_info_payload(Stream* s, PayloadBuilder* builder, uint32_t vsid)
{
    int status = 0;
    apm_module_param_data_t* header;
    uint8_t* ch_infoPayload = nullptr;
    Session* sess = nullptr;
    SessionAlsaVoice* session = nullptr;
    size_t ch_info_payloadSize = 0, padBytes = 0;
    uint8_t *ch_info_pl = nullptr;
    vcpm_param_id_tx_dev_pp_channel_info_t ch_info_payload;
    uint16_t channels = 0;

    PAL_DBG(LOG_TAG,"Enter");
    status = s->getAssociatedSession(&sess);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getAssociatedSession Failed \n");
        return status;
    }
    session = static_cast<SessionAlsaVoice*>(sess);
    status = session->getDeviceChannelInfo(s, &channels);
    if (status != 0) {
        PAL_ERR(LOG_TAG,"device get channel info failed");
        return status;
    }

    ch_info_payloadSize = sizeof(struct apm_module_param_data_t)+
                  sizeof(vcpm_param_id_tx_dev_pp_channel_info_t);
    padBytes = PAL_PADDING_8BYTE_ALIGN(ch_info_payloadSize);

    ch_infoPayload = (uint8_t *) calloc(1, (ch_info_payloadSize + padBytes));

    if (!ch_infoPayload) {
        PAL_ERR(LOG_TAG, "channel info payload allocation failed %s",
            strerror(errno));
        return -ENOMEM;
    }
    header = (apm_module_param_data_t*)ch_infoPayload;
    header->module_instance_id = VCPM_MODULE_INSTANCE_ID;
    header->param_id = VCPM_PARAM_ID_TX_DEV_PP_CHANNEL_INFO;
    header->error_code = 0x0;
    header->param_size = ch_info_payloadSize - sizeof(struct apm_module_param_data_t);

    ch_info_payload.vsid = vsid;
    ch_info_payload.num_channels = channels;
    PAL_DBG(LOG_TAG, "vsid %d num_channels %d", ch_info_payload.vsid,
                ch_info_payload.num_channels);
    ch_info_pl = (uint8_t*)ch_infoPayload + sizeof(apm_module_param_data_t);
    ar_mem_cpy(ch_info_pl,  sizeof(vcpm_param_id_tx_dev_pp_channel_info_t),
                     &ch_info_payload,  sizeof(vcpm_param_id_tx_dev_pp_channel_info_t));
    ch_info_payloadSize += padBytes;

    if (ch_infoPayload && ch_info_payloadSize) {
        status = builder->updateCustomPayload(ch_infoPayload, ch_info_payloadSize);
        builder->freeCustomPayload(&ch_infoPayload, &ch_info_payloadSize);
        if (status != 0) {
            PAL_ERR(LOG_TAG,"updateCustomPayload for channel info payload Failed %d\n",
                status);
            return status;
        }
    }
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int populateRatPayload(Stream* s, SessionAlsaVoice* session, PayloadBuilder* builder)
{
    int status = 0;
    int devId = 0;
    int idx = 0;
    uint32_t miid = 0;
    uint8_t* ratPayload = nullptr;
    size_t ratPayloadSize = 0;
    struct pal_device dAttr;
    struct pal_media_config config;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEnds;
    std::shared_ptr<ResourceManager> rm = nullptr;

    if(!s){
        PAL_ERR(LOG_TAG, "invalid stream pointer")
        status = -EINVAL;
        goto exit;
    }
    rm = ResourceManager::getInstance();
    status = s->getAssociatedDevices(associatedDevices);
    if ((0 != status) || (associatedDevices.size() == 0)) {
        PAL_ERR(LOG_TAG, "getAssociatedDevices fails or empty associated devices");
        goto exit;
    }
    for (idx = 0; idx < associatedDevices.size(); idx++) {
        devId = associatedDevices[idx]->getSndDeviceId();
        if (rm->isOutputDevId(devId)) {
            break;
        }
    }
    rxAifBackEnds = session->getRxBEVecRef();
    if (associatedDevices[idx]->isScoNbWbActive()) {
        status = session->getMIID(rxAifBackEnds[0].second.c_str(), RAT_RENDER, &miid);
        if (status != 0) {
            PAL_ERR(LOG_TAG,"getModuleInstanceId failed for RAT_RENDER: %X status: %d",
                RAT_RENDER, status);
            goto exit;
        }
        associatedDevices[idx]->getDeviceAttributes(&dAttr);
        builder->payloadRATConfig(&ratPayload, &ratPayloadSize, miid, &dAttr.config);
        if (ratPayload && ratPayloadSize) {
            status = builder->updateCustomPayload(ratPayload, ratPayloadSize);
            builder->freeCustomPayload(&ratPayload, &ratPayloadSize);
            if (status != 0)
                PAL_ERR(LOG_TAG,"updateCustomPayload for RAT_RENDER %XFailed\n", RAT_RENDER);
        }
    }

exit:
    return status;
}

int populate_rx_mfc_coeff_payload(std::shared_ptr<Device> CrsDevice, SessionAlsaVoice* session,
                                    PayloadBuilder* builder, std::vector<int>& pcmDevRxIds,
                                    std::shared_ptr<ResourceManager> rm)
{
    uint32_t miid = 0;
    int32_t status = 0;
    uint8_t* alsaParamData = nullptr;
    size_t alsaPayloadSize = 0;
    std::string backendname;
    struct mixer* mxr = nullptr;

    PAL_DBG(LOG_TAG,"Enter");
    status = rm->getVirtualAudioMixer(&mxr);
    if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }
    rm->getBackendName(CrsDevice->getSndDeviceId(), backendname);
    status = session->getMIID(backendname.c_str(), TAG_DEVICE_PP_MFC, &miid);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "Unable to get MIID");
        return status;
    }
    builder->payloadCRSMFCMixerCoeff((uint8_t **)&alsaParamData, &alsaPayloadSize, miid);
    if (alsaPayloadSize) {
        status = builder->updateCustomPayload(alsaParamData, alsaPayloadSize);
        builder->freeCustomPayload(&alsaParamData, &alsaPayloadSize);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "updateCustomPayload Failed\n");
            return status;
        }
    }
    builder->getCustomPayload(&alsaParamData, &alsaPayloadSize);
    status = SessionAlsaUtils::setMixerParameter(mxr,
                                                 pcmDevRxIds.at(0),
                                                 alsaParamData,
                                                 alsaPayloadSize);
    builder->freeCustomPayload();
    if (status != 0) {
        PAL_ERR(LOG_TAG, "setMixerParameter failed");
        return status;
    }
exit:
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int voicePluginReconfig(Stream* s, void* pluginPayload)
{
    int status;
    struct ReconfigPluginPayload* reconfigPld = nullptr;

    reconfigPld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);

    if (!reconfigPld->config_ctrl.compare("silence_detection")) {
       status = voiceSilenceDetectionConfig(SD_ENABLE, &reconfigPld->dAttr, pluginPayload);
    } else {
           status = reconfigCommon(s, pluginPayload);
           if (status) {
               PAL_DBG(LOG_TAG,"reconfigCommon failed: %d", status);
               return status;
           }
           voicePostCommonReconfig(s, pluginPayload);
    }
    PAL_DBG(LOG_TAG,"Exit ret: %d", status);
    return status;
}

int voiceSilenceDetectionConfig(uint8_t config, pal_device *dAttr, void * pluginPayload) {
    int status = 0;
    uint32_t miid = 0;
    size_t pad_bytes = 0, payloadSize = 0;
    uint8_t* payload = NULL;
    struct apm_module_param_data_t* header = NULL;
    param_id_silence_detection_t *silence_detection_cfg = NULL;
    std::shared_ptr<Device> dev = nullptr;
    ReconfigPluginPayload* ppld = nullptr;
    SessionAlsaVoice* session = nullptr;
    struct mixer* mxr = nullptr;
    PayloadBuilder* builder = nullptr;
    std::vector<int> pcmDevIds;
    std::vector<std::pair<int32_t, std::string>> txAifBackEnds;
    std::shared_ptr<ResourceManager> rm = nullptr;

    if (!rm->IsSilenceDetectionEnabledVoice())
        return 0;

    ppld = reinterpret_cast<ReconfigPluginPayload*>(pluginPayload);
    builder = reinterpret_cast<PayloadBuilder*>(ppld->builder);
    session = static_cast<SessionAlsaVoice*>(ppld->session);
    rm = ResourceManager::getInstance();
    status = rm->getVirtualAudioMixer(&mxr);
      if (status) {
        PAL_ERR(LOG_TAG, "mixer error");
        goto exit;
    }

    status = session->getFrontEndIds(pcmDevIds, TX_HOSTLESS);
    if (status) {
        PAL_ERR(LOG_TAG, "getFrontEndIds failed %d", status);
        goto exit;
    }
    txAifBackEnds = session->getTxBEVecRef();

    switch(config) {
        case SD_DISCONNECT:
            (void) disableSilenceDetection(rm, mxr,
                pcmDevIds, txAifBackEnds[0].second.data(), (uint64_t)session);
            break;
        case SD_CONNECT:
            (void) enableSilenceDetection(rm, mxr,
                pcmDevIds, txAifBackEnds[0].second.data(), (uint64_t)session);
           break;
        case SD_ENABLE:
        case SD_SETPARAM:
            status =  SessionAlsaUtils::getModuleInstanceId(mxr,
                pcmDevIds.at(0), txAifBackEnds[0].second.data(), DEVICE_HW_ENDPOINT_TX, &miid);
            if (status != 0) {
                PAL_ERR(LOG_TAG, "Error retrieving MIID for HW_ENDPOINT_TX\n");
                return -SD_ENABLE;
            }
            payloadSize = sizeof(struct apm_module_param_data_t) +
                                 sizeof(param_id_silence_detection_t);
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
            if (config == SD_ENABLE) {
                dev = Device::getInstance(dAttr, rm);
                if (!dev) {
                    PAL_ERR(LOG_TAG, "Device creation failed");
                    return -SD_ENABLE;
                }

                status = dev->updateCustomPayload(payload, payloadSize);
                dev->freeCustomPayload(&payload, &payloadSize);
                if (status) {
                    PAL_ERR(LOG_TAG, "update device custom payload failed\n");
                    return -SD_ENABLE;
                }
            } else {
                status = SessionAlsaUtils::setMixerParameter(mxr, pcmDevIds.at(0),
                                                             payload, payloadSize);
                if (status) {
                    PAL_ERR(LOG_TAG, "Silence Detection enable param failed\n");
                    builder->freeCustomPayload(&payload, &payloadSize);
                    return -SD_ENABLE;
                }
            }
            builder->freeCustomPayload(&payload, &payloadSize);
            break;
            default:
                PAL_ERR(LOG_TAG, "Invalid config for Silence Detection\n");
    };
exit:
    return status;
}
