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

#define LOG_TAG "StreamCallTranslation"

#include "StreamCallTranslation.h"
#include "Session.h"
#include "ResourceManager.h"
#include <unistd.h>
#ifndef PAL_MEMLOG_UNSUPPORTED
#include "MemLogBuilder.h"
#endif

extern "C" Stream* CreateCallTranslationStream(const struct pal_stream_attributes *sattr, struct pal_device *dattr,
                                  const uint32_t no_of_devices, const struct modifier_kv *modifiers,
                                  const uint32_t no_of_modifiers, const std::shared_ptr<ResourceManager> rm) {
    try {
        return new StreamCallTranslation(sattr, dattr, no_of_devices,
                                         modifiers, no_of_modifiers, rm);
    } catch (const std::exception& e) {
         PAL_ERR(LOG_TAG, "Stream create failed for stream type %s: %s",
                 streamNameLUT.at(sattr->type).c_str(), e.what());
        return nullptr;
    }
}

StreamCallTranslation::StreamCallTranslation(const struct pal_stream_attributes *sattr __unused,
                       struct pal_device *dattr __unused, const uint32_t no_of_devices __unused,
                       const struct modifier_kv *modifiers __unused, const uint32_t no_of_modifiers __unused,
                       const std::shared_ptr<ResourceManager> rm)
{
    mStreamMutex.lock();
    uint32_t in_channels = 0, out_channels = 0;
    uint32_t attribute_size = 0;

    session = NULL;
    mGainLevel = -1;
    outputTxConfig = nullptr;
    outputRxConfig = nullptr;
    palCallTranslationConfig = nullptr;
    std::shared_ptr<Device> dev = nullptr;
    mStreamAttr = (struct pal_stream_attributes *)nullptr;
    mDevices.clear();
    currentState = STREAM_IDLE;
    //Modify cached values only at time of SSR down.
    cachedState = STREAM_IDLE;
    cookie_ = 0;
    bool isDeviceConfigUpdated = false;

    PAL_DBG(LOG_TAG, "Enter");

    //TBD handle modifiers later
    mNoOfModifiers = 0; //no_of_modifiers;
    mModifiers = (struct modifier_kv *) (NULL);
    std::ignore = modifiers;
    std::ignore = no_of_modifiers;

    if (!sattr) {
        PAL_ERR(LOG_TAG,"Error:invalid arguments");
        mStreamMutex.unlock();
        throw std::runtime_error("invalid arguments");
    }

    attribute_size = sizeof(struct pal_stream_attributes);
    mStreamAttr = (struct pal_stream_attributes *) calloc(1, attribute_size);
    if (!mStreamAttr) {
        PAL_ERR(LOG_TAG, "Error:malloc for stream attributes failed %s", strerror(errno));
        mStreamMutex.unlock();
        throw std::runtime_error("failed to malloc for stream attributes");
    }

    memcpy(mStreamAttr, sattr, sizeof(pal_stream_attributes));

    if (mStreamAttr->in_media_config.ch_info.channels > PAL_MAX_CHANNELS_SUPPORTED) {
        PAL_ERR(LOG_TAG,"Error:in_channels is invalid %d", in_channels);
        mStreamAttr->in_media_config.ch_info.channels = PAL_MAX_CHANNELS_SUPPORTED;
    }
    if (mStreamAttr->out_media_config.ch_info.channels > PAL_MAX_CHANNELS_SUPPORTED) {
        PAL_ERR(LOG_TAG,"Error:out_channels is invalid %d", out_channels);
        mStreamAttr->out_media_config.ch_info.channels = PAL_MAX_CHANNELS_SUPPORTED;
    }

    PAL_VERBOSE(LOG_TAG, "Create new Session for stream type %d", sattr->type);
    session = Session::makeSession(rm, sattr);
    if (!session) {
        PAL_ERR(LOG_TAG, "Error:session creation failed");
        mStreamMutex.unlock();
        throw std::runtime_error("failed to create session object");
    }

    PAL_VERBOSE(LOG_TAG, "Create new Devices with no_of_devices - %d", no_of_devices);
    /* update handset/speaker sample rate for UPD with shared backend */
    if ((sattr->type == PAL_STREAM_ULTRASOUND ||
         sattr->type == PAL_STREAM_SENSOR_PCM_RENDERER) && !rm->IsDedicatedBEForUPDEnabled()) {
        struct pal_device devAttr = {};
        struct pal_device_info inDeviceInfo;
        pal_device_id_t upd_dev[] = {PAL_DEVICE_OUT_SPEAKER, PAL_DEVICE_OUT_HANDSET};
        for (int i = 0; i < sizeof(upd_dev)/sizeof(upd_dev[0]); i++) {
            devAttr.id = upd_dev[i];
            dev = Device::getInstance(&devAttr, rm);
            if (!dev)
                continue;
            rm->getDeviceInfo(devAttr.id, sattr->type, "", &inDeviceInfo);
            dev->setSampleRate(inDeviceInfo.samplerate);
            if (devAttr.id == PAL_DEVICE_OUT_HANDSET)
                dev->setBitWidth(inDeviceInfo.bit_width);
        }
    }

    callTranslationConfigPayload = (call_translation_config *)malloc(sizeof(call_translation_config));
    if (callTranslationConfigPayload == nullptr) {
        PAL_ERR(LOG_TAG, "callTranslationConfigPayload Memory allocation failed");
        return;
    }

    for (int i = 0; i < no_of_devices; i++) {
        //Check with RM if the configuration given can work or not
        //for e.g., if incoming stream needs 24 bit device thats also
        //being used by another stream, then the other stream should route

        dev = Device::getInstance((struct pal_device *)&dattr[i] , rm);
        if (!dev) {
            PAL_ERR(LOG_TAG, "Error:Device creation failed");
            //TBD::free session too
            mStreamMutex.unlock();
            throw std::runtime_error("failed to create device object");
        }
        dev->insertStreamDeviceAttr(&dattr[i], this);
        mPalDevices.push_back(dev);
        mStreamMutex.unlock();
        // streams with VA MIC is handled in rm::handleConcurrentStreamSwitch()
        if (dattr[i].id != PAL_DEVICE_IN_HANDSET_VA_MIC &&
            dattr[i].id != PAL_DEVICE_IN_HEADSET_VA_MIC)
            isDeviceConfigUpdated = rm->updateDeviceConfig(&dev, &dattr[i], sattr);
        mStreamMutex.lock();

        if (isDeviceConfigUpdated)
            PAL_VERBOSE(LOG_TAG, "Device config updated");

        /* Create only update device attributes first time so update here using set*/
        /* this will have issues if same device is being currently used by different stream */
        mDevices.push_back(dev);
    }
    mStreamMutex.unlock();
    rm->registerStream(this);

    NMTEngine = CallTranslationNMTEngine::GetInstance(this);
    if (!NMTEngine) {
        PAL_ERR(LOG_TAG, "Error:%d NMT engine creation failed");
        throw std::runtime_error("failed to create NMT engine");
    }
    PAL_DBG(LOG_TAG, "Exit. state %d", currentState);
    return;
}

StreamCallTranslation::~StreamCallTranslation() {
    rm->deregisterStream(this);
}

int32_t  StreamCallTranslation::open()
{
    int32_t status = 0;

    PAL_DBG(LOG_TAG, "Enter. session handle - %pK device count - %zu", session,
            mDevices.size());

    mStreamMutex.lock();
    if (PAL_CARD_STATUS_DOWN(rm->getSoundCardState())) {
        PAL_ERR(LOG_TAG, "Error:Sound card offline/standby, can not open stream");
        usleep(SSR_RECOVERY);
        status = -EIO;
        goto exit;
    }

    if (currentState == STREAM_IDLE) {
        for (int32_t i = 0; i < mDevices.size(); i++) {
            status = mDevices[i]->open();
            if (0 != status) {
                PAL_ERR(LOG_TAG, "Error:device open failed with status %d", status);
                goto exit;
            }
        }

        status = session->open(this);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "Error:session open failed with status %d", status);
            goto closeDevice;
        }
        PAL_VERBOSE(LOG_TAG, "session open successful");
        currentState = STREAM_INIT;
        PAL_DBG(LOG_TAG, "streamLL opened. state %d", currentState);
        goto exit;
    } else if (currentState == STREAM_INIT) {
        PAL_INFO(LOG_TAG, "Stream is already opened, state %d", currentState);
        status = 0;
        goto exit;
    } else {
        PAL_ERR(LOG_TAG, "Error:Stream is not in correct state %d", currentState);
        //TBD : which error code to return here.
        status = -EINVAL;
        goto exit;
    }
closeDevice:
    for (int32_t i = 0; i < mDevices.size(); i++) {
        status = mDevices[i]->close();
        if (0 != status) {
            PAL_ERR(LOG_TAG, "device close is failed with status %d", status);
        }
    }
exit:
#ifndef PAL_MEMLOG_UNSUPPORTED
    palStateEnqueue(this, PAL_STATE_OPENED, status);
#endif
    mStreamMutex.unlock();
    PAL_DBG(LOG_TAG, "Exit ret %d", status)
    return status;
}

int32_t  StreamCallTranslation::close()
{
    int32_t status = 0;
    mStreamMutex.lock();

    if (currentState == STREAM_IDLE) {
        PAL_INFO(LOG_TAG, "Stream is already closed");
        mStreamMutex.unlock();
        return status;
    }

    PAL_DBG(LOG_TAG, "Enter. session handle - %pK device count - %zu stream_type - %d state %d",
             session, mDevices.size(), mStreamAttr->type, currentState);
    if (currentState == STREAM_STARTED || currentState == STREAM_PAUSED) {
        mStreamMutex.unlock();
        status = stop();
        if (0 != status)
            PAL_ERR(LOG_TAG, "Error:stream stop failed. status %d",  status);
        mStreamMutex.lock();
    }

    rm->lockGraph();
    status = session->close(this);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "Error:session close failed with status %d", status);
    }

    for (int32_t i = 0; i < mDevices.size(); i++) {
        status = mDevices[i]->close();
        if (0 != status) {
            PAL_ERR(LOG_TAG, "Error:device close is failed with status %d", status);
        }
    }
    PAL_VERBOSE(LOG_TAG, "closed the devices successfully");
    currentState = STREAM_IDLE;
    rm->unlockGraph();
    rm->checkAndSetDutyCycleParam();
#ifndef PAL_MEMLOG_UNSUPPORTED
    palStateEnqueue(this, PAL_STATE_CLOSED, status);
#endif
    mStreamMutex.unlock();

    if (callTranslationConfigPayload) {
        free(callTranslationConfigPayload);
        callTranslationConfigPayload = nullptr;
    }
    if (palCallTranslationConfig) {
        free(palCallTranslationConfig);
        palCallTranslationConfig = nullptr;
    }
    PAL_DBG(LOG_TAG, "Exit. closed the stream successfully %d status %d",
             currentState, status);
    return status;
}

int32_t StreamCallTranslation::start()
{
    int32_t status = 0;

    PAL_DBG(LOG_TAG, "Enter. session handle - %pK mStreamAttr->direction - %d state %d",
            session, mStreamAttr->direction, currentState);

    mStreamMutex.lock();

    if (PAL_CARD_STATUS_DOWN(rm->getSoundCardState())) {
        cachedState = STREAM_STARTED;
        PAL_ERR(LOG_TAG, "Error:Sound card offline/standby. Update the cached state %d",
                cachedState);
        goto exit;
    }

    if (currentState == STREAM_INIT || currentState == STREAM_STOPPED) {
        rm->lockGraph();
        status = start_device();
        if (0 != status) {
            rm->unlockGraph();
            goto exit;
        }
        PAL_VERBOSE(LOG_TAG, "device started successfully");
        if (NMTEngine) {
            status = NMTEngine->StartEngine(this);
            if (0 != status) {
                status = -ENOMEM;
                PAL_ERR(LOG_TAG, "Error:%d Start NMT engine failed", status);
            }
        }
        status = startSession();
        if (0 != status) {
            rm->unlockGraph();
            goto exit;
        }
        rm->unlockGraph();
        PAL_VERBOSE(LOG_TAG, "session start successful");

        /*pcm_open and pcm_start done at once here,
         *so directly jump to STREAM_STARTED state.
         */
        currentState = STREAM_STARTED;
        mStreamMutex.unlock();
        rm->lockActiveStream();
        mStreamMutex.lock();
        for (int i = 0; i < mDevices.size(); i++) {
            rm->registerDevice(mDevices[i], this);
        }
        rm->unlockActiveStream();
        rm->checkAndSetDutyCycleParam();
    } else if (currentState == STREAM_STARTED) {
        PAL_INFO(LOG_TAG, "Stream already started, state %d", currentState);
    } else {
        PAL_ERR(LOG_TAG, "Error:Stream is not opened yet");
        status = -EINVAL;
    }
exit:
#ifndef PAL_MEMLOG_UNSUPPORTED
    palStateEnqueue(this, PAL_STATE_STARTED, status);
#endif
    PAL_DBG(LOG_TAG, "Exit. state %d", currentState);
    mStreamMutex.unlock();
    return status;
}

int32_t StreamCallTranslation::start_device()
{
    int32_t status = 0;
    for (int32_t i=0; i < mDevices.size(); i++) {
         status = mDevices[i]->start();
         if (0 != status) {
             PAL_ERR(LOG_TAG, "Error:%s device start is failed with status %d",
                     GET_DIR_STR(mStreamAttr->direction), status);
         }
    }
    return status;
}

int32_t StreamCallTranslation::startSession()
{
    int32_t status = 0, devStatus = 0;
    status = session->prepare(this);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "Error:%s session prepare is failed with status %d",
                    GET_DIR_STR(mStreamAttr->direction), status);
        goto session_fail;
    }
    PAL_VERBOSE(LOG_TAG, "session prepare successful");

    status = session->start(this);
    if (errno == -ENETRESET) {
        if (PAL_CARD_STATUS_DOWN(rm->getSoundCardState())) {
            PAL_ERR(LOG_TAG, "Error:Sound card offline/standby, informing RM");
            rm->ssrHandler(CARD_STATUS_OFFLINE);
        }
        cachedState = STREAM_STARTED;
        status = 0;
        goto session_fail;
    }
    if (0 != status) {
        PAL_ERR(LOG_TAG, "Error:%s session start is failed with status %d",
                    GET_DIR_STR(mStreamAttr->direction), status);
        goto session_fail;
    }
    goto exit;

session_fail:
    for (int32_t i=0; i < mDevices.size(); i++) {
        devStatus = mDevices[i]->stop();
        if (devStatus)
            status = devStatus;
    }
exit:
    return status;
}

int32_t StreamCallTranslation::stop()
{
    int32_t status = 0;

    mStreamMutex.lock();
    PAL_DBG(LOG_TAG, "Enter. session handle - %pK mStreamAttr->direction - %d state %d",
                session, mStreamAttr->direction, currentState);

    if (currentState == STREAM_STARTED || currentState == STREAM_PAUSED) {
        mStreamMutex.unlock();
        rm->lockActiveStream();
        mStreamMutex.lock();
        currentState = STREAM_STOPPED;

        if (NMTEngine) {
            status = NMTEngine->StopEngine(this);
            if (status) {
               PAL_ERR(LOG_TAG, "Error:%d Stop NMT engine failed", status);
            }
        }
        for (int i = 0; i < mDevices.size(); i++) {
            rm->deregisterDevice(mDevices[i], this);
        }
        rm->unlockActiveStream();
        PAL_VERBOSE(LOG_TAG, "In %s, device count - %zu",
                    GET_DIR_STR(mStreamAttr->direction), mDevices.size());

        rm->lockGraph();
        status = session->stop(this);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "Error:%s session stop failed with status %d",
                    GET_DIR_STR(mStreamAttr->direction), status);
        }
        PAL_VERBOSE(LOG_TAG, "session stop successful");
        for (int32_t i=0; i < mDevices.size(); i++) {
             status = mDevices[i]->stop();
             if (0 != status) {
                 PAL_ERR(LOG_TAG, "Error:%s device stop failed with status %d",
                         GET_DIR_STR(mStreamAttr->direction), status);
             }
        }
        rm->unlockGraph();
        PAL_VERBOSE(LOG_TAG, "devices stop successful");
    } else if (currentState == STREAM_STOPPED || currentState == STREAM_IDLE) {
        PAL_INFO(LOG_TAG, "Stream is already in Stopped state %d", currentState);
    } else {
        PAL_ERR(LOG_TAG, "Error:Stream should be in start/pause state, %d", currentState);
        status = -EINVAL;
    }
#ifndef PAL_MEMLOG_UNSUPPORTED
    palStateEnqueue(this, PAL_STATE_STOPPED, status);
#endif
    PAL_DBG(LOG_TAG, "Exit. status %d, state %d", status, currentState);

    mStreamMutex.unlock();
    return status;
}

int32_t StreamCallTranslation::getParameters(uint32_t param_id, void **payload)
{
    int32_t ret = 0;

    if (!NMTEngine) {
        PAL_ERR(LOG_TAG, "Error: NMTEngine is null");
        return -EINVAL;
    }
    if (param_id == PAL_PARAM_NMT_GET_NUM_EVENT) {
        ret = NMTEngine->GetNumOutput();
    } else if (param_id == PAL_PARAM_NMT_GET_OUTPUT_TOKEN) {
        ret = NMTEngine->GetOutputToken();
    } else if (param_id == PAL_PARAM_NMT_GET_PAYLOAD_SIZE) {
        ret = NMTEngine->GetPayloadSize();
    }
    return ret;
}

bool StreamCallTranslation::compareConfig(struct call_translation_config *oldConfig,
                                          struct call_translation_config *newConfig)
{
    if (newConfig == nullptr)
        return true;
    if (oldConfig == nullptr ||
        oldConfig->enable != newConfig->enable ||
        oldConfig->call_translation_dir != newConfig->call_translation_dir ||
        oldConfig->nmt_module_config.input_language_code != newConfig->nmt_module_config.input_language_code ||
        oldConfig->nmt_module_config.output_language_code != newConfig->nmt_module_config.output_language_code) {
        return false;
    }
    return true;
}

int32_t StreamCallTranslation::setNMTConfig(struct pal_nmt_config *nmt_payload)
{
    int32_t status = 0;

    nmtConfig.input_language_code  = (uint32_t)nmt_payload->input_language_code;
    nmtConfig.output_language_code = (uint32_t)nmt_payload->output_language_code;
    if (mStreamAttr->direction == PAL_AUDIO_INPUT) {
        if (outputTxConfig) {
            free(outputTxConfig);
            outputTxConfig = nullptr;
        }
        outputTxConfig = (param_id_nmt_output_config_t *)calloc(1, sizeof(param_id_nmt_output_config_t));
        if (!outputTxConfig) {
            status = -ENOMEM;
            PAL_ERR(LOG_TAG, "Error:%d Failed to allocate outputTxConfig", status);
            goto cleanup;
        }
        outputTxConfig->output_mode  = NMT::NON_BUFFERED;
        outputTxConfig->out_buf_size = (uint32_t)OUT_BUF_SIZE_DEFAULT;
        outputTxConfig->num_bufs     = 1;
    } else if (mStreamAttr->direction == PAL_AUDIO_OUTPUT) {
        if (outputRxConfig) {
            free(outputRxConfig);
            outputRxConfig = nullptr;
        }
        outputRxConfig = (param_id_nmt_output_config_t *)calloc(1, sizeof(param_id_nmt_output_config_t));
        if (!outputRxConfig) {
            status = -ENOMEM;
            PAL_ERR(LOG_TAG, "Error:%d Failed to allocate outputRxConfig", status);
            goto cleanup;
        }
        outputRxConfig->output_mode  = NMT::NON_BUFFERED;
        outputRxConfig->out_buf_size = (uint32_t)OUT_BUF_SIZE_DEFAULT;
        outputRxConfig->num_bufs     = 1;
    }
    goto exit;

cleanup:
    if (outputTxConfig) {
        free(outputTxConfig);
        outputTxConfig = nullptr;
    }
    if (outputRxConfig) {
        free(outputRxConfig);
        outputRxConfig = nullptr;
    }

exit:
    PAL_DBG(LOG_TAG, "Exit, status %d", status);
    return status;
}

int32_t StreamCallTranslation::setCallTranslationConfig(struct call_translation_config *payload)
{
    PAL_INFO(LOG_TAG, "Enter");

    int32_t status = 0;
    if (!payload) {
        status = -EINVAL;
        PAL_ERR(LOG_TAG, "wrong params");
        return status;
    }

    if (compareConfig(palCallTranslationConfig, payload)) {
        PAL_DBG(LOG_TAG, "Same NMT config, no need to set it again!!!");
        return status;
    }

    pal_nmt_config* nmt_payload = nullptr;
    pal_call_translation_direction call_translation_dir = payload->call_translation_dir;

    if (palCallTranslationConfig) {
        free(palCallTranslationConfig);
        palCallTranslationConfig = nullptr;
    }

    if (call_translation_dir == CALL_TRANSLATION_DIR_TX ||
        call_translation_dir == CALL_TRANSLATION_DIR_RX) {
        nmt_payload = &(payload->nmt_module_config);
        if (!nmt_payload) {
            status = -EINVAL;
            goto exit;
        }
        if (payload->enable) {
            status = setNMTConfig(nmt_payload);
            if (status) {
                goto exit;
            }
        }
    }
    palCallTranslationConfig = (struct call_translation_config *)calloc(1,
        sizeof(struct call_translation_config));
    if (!palCallTranslationConfig) {
        PAL_ERR(LOG_TAG, "Error: Failed to allocate memory for palCallTranslationConfig");
        status = -ENOMEM;
        goto exit;
    }
    status = ar_mem_cpy(palCallTranslationConfig, sizeof(struct call_translation_config),
                                         payload, sizeof(struct call_translation_config));
    if (status) {
        PAL_ERR(LOG_TAG, "Error: Failed to copy call translation config");
        free(palCallTranslationConfig);
        palCallTranslationConfig = nullptr;
    }
exit:
    PAL_DBG(LOG_TAG, "exit, process parameter status %d", status);
    return status;
}

int32_t  StreamCallTranslation::setParameters(uint32_t param_id, void *payload)
{
    int32_t status = 0;
    PAL_INFO(LOG_TAG, "Enter, set parameter %u, session handle - %p", param_id, session);
    pal_param_payload *param_payload = NULL;

    if (param_id == PAL_PARAM_ID_NMT_OUTPUT) {
        if (NMTEngine) {
            NMTEngine->setParameters(this, (pal_param_id_type_t) param_id);
            goto exit;
        }
    }
    if (!payload) {
        status = -EINVAL;
        PAL_ERR(LOG_TAG, "wrong params");
        goto exit;
    }
    mStreamMutex.lock();
    if (currentState == STREAM_IDLE) {
        status = -EINVAL;
        PAL_ERR(LOG_TAG, "Invalid stream state: IDLE for param ID: %d", param_id);
        mStreamMutex.unlock();
        goto exit;
    }
    // Check if session is not null
    if (session != NULL) {
        PAL_DBG(LOG_TAG, ": Found a session. Copy the payload to stream object callTranslationConfigPayload.");
        param_payload = (pal_param_payload *)payload;
        ar_mem_cpy(callTranslationConfigPayload, sizeof(call_translation_config), param_payload->payload, sizeof(call_translation_config));
        PAL_DBG(LOG_TAG, ": callTranslationConfigPayload : enable=%d, call_translation_dir=%d, asr_module_config: input_language_code=%d, output_language_code=%d,"
                         " enable_language_detection=%d, enable_translation=%d, enable_continuous_mode=%d, enable_partial_transcription=%d, threshold=%d, timeout_duration=%d,"
                         " silence_detection_duration=%d, outputBufferMode=%d, nmt_module_config: input_language_code=%d, output_language_code=%d,"
                         " tts_module_config: language_code=%d, speech_format=%d", callTranslationConfigPayload->enable, callTranslationConfigPayload->call_translation_dir, callTranslationConfigPayload->asr_module_config.input_language_code,
                         callTranslationConfigPayload->asr_module_config.output_language_code, callTranslationConfigPayload->asr_module_config.enable_language_detection, callTranslationConfigPayload->asr_module_config.enable_translation,
                         callTranslationConfigPayload->asr_module_config.enable_continuous_mode, callTranslationConfigPayload->asr_module_config.enable_partial_transcription, callTranslationConfigPayload->asr_module_config.threshold,
                         callTranslationConfigPayload->asr_module_config.timeout_duration, callTranslationConfigPayload->asr_module_config.silence_detection_duration, callTranslationConfigPayload->asr_module_config.outputBufferMode,
                         callTranslationConfigPayload->nmt_module_config.input_language_code, callTranslationConfigPayload->nmt_module_config.output_language_code, callTranslationConfigPayload->tts_module_config.language_code,
                         callTranslationConfigPayload->tts_module_config.speech_format);
        status = setCallTranslationConfig(callTranslationConfigPayload);
        if (status) {
            PAL_ERR(LOG_TAG, "Error:%d process translation cfg", status);
        }
    } else {
        PAL_ERR(LOG_TAG, "session is null");
        status = -EINVAL;
    }
    mStreamMutex.unlock();
exit:
    PAL_DBG(LOG_TAG, "exit, session parameter %u set with status %d", param_id, status);
    return status;
}

void StreamCallTranslation::HandleEventData(eventPayload engEvent)
{
     std::lock_guard<std::mutex> lock(mStreamMutex);
     if (!engEvent.payload) {
          return;
     }

     pal_callback_config_t config = {};
     uint32_t eventId = engEvent.type;
     bool validEvent = true;
     int status = 0;
     switch (eventId) {
         case  CALL_TRANSLATION_INOUT_TEXT : {
             pal_nmt_event*event = (pal_nmt_event *)engEvent.payload;
             PAL_INFO(LOG_TAG, "Call translation IO text event, event status : %d, num events : %d",
                      event->status, event->num_events);
             for (int i = 0; i < event->num_events; ++i) {
                  PAL_DBG(LOG_TAG, "Event no : %d,input text_size : %d, input text : %s", i,
                          event->event[i].input_text_size, event->event[i].input_text);
                  PAL_DBG(LOG_TAG, "Event no : %d,output text_size : %d, output text : %s", i,
                          event->event[i].output_text_size, event->event[i].output_text);
             }
             break;
         }
         case CALL_TRANSLATION_IN_TEXT : {
             pal_nmt_event *event = (pal_nmt_event *)engEvent.payload;
             PAL_INFO(LOG_TAG, "Call translation IN text event, event status : %d, num events : %d",
                      event->status, event->num_events);
             for (int i = 0; i < event->num_events; ++i) {
                  PAL_DBG(LOG_TAG, "Event no : %d,input text_size : %d, input text : %s", i,
                          event->event[i].input_text_size, event->event[i].input_text);
             }
             break;
         }
         case CALL_TRANSLATION_OUT_TEXT : {
             pal_nmt_event *event = (pal_nmt_event *)engEvent.payload;
             PAL_INFO(LOG_TAG, "Call translation OUT text event, event status : %d, num events : %d",
                      event->status, event->num_events);
             for (int i = 0; i < event->num_events; ++i) {
                  PAL_DBG(LOG_TAG, "Event no : %d, text_size : %d, text : %s", i,
                  event->event[i].output_text_size, event->event[i].output_text);
             }
             break;

         }
         default : {
             validEvent = false;
             PAL_INFO(LOG_TAG, "Invalid event received, ignore!!!");
         }
     }

     if (validEvent && rm->callback_event != NULL) {
         config.event = (uint32_t *)engEvent.payload;
         status = rm->callback_event(&config, PAL_NOTIFY_CALL_TRANSLATION_TEXT, false);
         if (status != 0) {
             PAL_ERR(LOG_TAG, "Error: Callback event failed with status %d", status);
         }
     }
}
