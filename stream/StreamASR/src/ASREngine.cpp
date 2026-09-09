/*
 * Copyright (c) 2021-2022, The Linux Foundation. All rights reserved.
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

#define ATRACE_TAG (ATRACE_TAG_AUDIO | ATRACE_TAG_HAL)
#define LOG_TAG "PAL: ASREngine"

#if defined(MUSL_SYS_LIBRARIES)
#ifdef __unused
#undef __unused
#endif
#define __unused
#endif

#include "ASREngine.h"

#include <cmath>
#ifdef PAL_CUTILS_SUPPORTED
#include <cutils/trace.h>
#endif
#include <string.h>
#include "Session.h"
#include "SessionAR.h"
#include "StreamASR.h"
#include "ResourceManager.h"
#include "kvh2xml.h"

#define ASR_DBG_LOGS
#ifdef ASR_DBG_LOGS
#define PAL_DBG(LOG_TAG,...)  PAL_INFO(LOG_TAG,__VA_ARGS__)
#endif

#define FILENAME_LEN 128
std::shared_ptr<ASREngine> ASREngine::eng;

ASREngine::ASREngine(StreamASR *s, std::shared_ptr<ASRStreamConfig> smCfg)
{
    PAL_DBG(LOG_TAG, "Enter");
    int status = 0;
    struct pal_stream_attributes sAttr;
    std::shared_ptr<ResourceManager> rm = nullptr;
    static std::shared_ptr<ASREngine> eng = nullptr;

    isCrrDevUsingExtEc = false;
    exitThread = false;
    loggerModeEnabled = false;
    timestampEnabled = false;
    outputBufSize = 0;
    ecRefCount = 0;
    devDisconnectCount = 0;
    numOutput = 0;
    rxEcDev = nullptr;
    asrInfo = nullptr;
    smCfg = smCfg;
    engState = ASR_ENG_IDLE;
    builder = new PayloadBuilder();
    speechCfg = nullptr;
    outputCfg = nullptr;
    sdzOutputCfg = nullptr;
    outputCfgSize = 0;
    sdzOutputCfgSize = 0;

    asrInfo = ASRPlatformInfo::GetInstance();
    if (!asrInfo) {
        PAL_ERR(LOG_TAG, "No ASR platform present");
        throw std::runtime_error("No ASR platform present");
    }

    for (int i = ASR_INPUT_CONFIG; i < ASR_MAX_PARAM_IDS; i++) {
        paramIds[i] = smCfg->GetParamId((asr_param_id_type)i);
        moduleTagIds[i] = smCfg->GetModuleTagId((asr_param_id_type)i);
    }

    status = s->getStreamAttributes(&sAttr);
    if (status) {
        PAL_ERR(LOG_TAG, "Failed to get stream attributes");
        throw std::runtime_error("Failed to get stream attributes");
   }

    rm = ResourceManager::getInstance();
    if (!rm) {
        PAL_ERR(LOG_TAG, "Failed to get ResourceManager instance");
        throw std::runtime_error("Failed to get ResourceManager instance");
    }

    session = Session::makeSession(rm, &sAttr);
    if (!session) {
        PAL_ERR(LOG_TAG, "Failed to create session");
        throw std::runtime_error("Failed to create session");
    }

    session->registerCallBack(HandleSessionCallBack, (uint64_t)this);

    eventThreadHandler = std::thread(ASREngine::EventProcessingThread, this);
    if (!eventThreadHandler.joinable()) {
        PAL_ERR(LOG_TAG, "Error:%d failed to create event processing thread",
               status);
        throw std::runtime_error("Failed to create event processing thread");
    }

    PAL_DBG(LOG_TAG, "Exit");
}

ASREngine::~ASREngine()
{
    PAL_INFO(LOG_TAG, "Enter");

    smCfg = nullptr;
    asrInfo = nullptr;
    session = nullptr;

    if (builder) {
        delete builder;
        builder = nullptr;
    }

    if (speechCfg) {
        free(speechCfg);
        speechCfg = nullptr;
    }
    if (outputCfg) {
        free(outputCfg);
        outputCfg = nullptr;
    }
    if (sdzOutputCfg) {
        free(sdzOutputCfg);
        sdzOutputCfg = nullptr;
    }

    {
        std::unique_lock<std::mutex> lck(mutexEngine);
        exitThread = true;
        cv.notify_one();
    }

    if (eventThreadHandler.joinable()) {
        eventThreadHandler.join();
    }

    PAL_INFO(LOG_TAG, "Exit");
}

std::shared_ptr<ASREngine> ASREngine::GetInstance(
     StreamASR *s,
     std::shared_ptr<ASRStreamConfig> smCfg)
{
     if (!eng)
         eng = std::make_shared<ASREngine>(s, smCfg);

     return eng;
}

bool ASREngine::IsEngineActive()
{
    if (engState == ASR_ENG_ACTIVE ||
        engState == ASR_ENG_TEXT_RECEIVED)
        return true;

    return false;
}

int32_t ASREngine::UpdateASRConfiguration(StreamASR *s)
{
    int32_t status = 0;
    uint8_t *ev_reg_payload = nullptr;
    size_t ev_reg_size = 0;
    std::vector<asr_param_id_type_t> param_id_list;
    struct event_id_asr_output_reg_cfg_t *asr_reg_cfg = nullptr;
    struct event_id_asr_output_reg_cfg_v2_t *asr_reg_v2_cfg = nullptr;
    asr_client_id_t client_id = ASR_CLIENT_INVALID;
    param_id_asr_output_config_t *asr_out_cfg = nullptr;
    param_id_asr_output_config_v2_base_t *client_configs = nullptr;
    uint32_t event_id = 0;

    if (!s) {
        PAL_ERR(LOG_TAG, "Invalid stream pointer");
        return -EINVAL;
    }

    client_id = s->GetClientId();
    if (client_id == ASR_CLIENT_INVALID) {
        ev_reg_size = sizeof(struct event_id_asr_output_reg_cfg_t);
        ev_reg_payload = (uint8_t *)calloc(1, ev_reg_size);
        if (ev_reg_payload == NULL) {
            PAL_ERR(LOG_TAG, "Failed to allocate event payload");
            status = -ENOMEM;
            goto exit;
        }
        asr_reg_cfg =
            (struct event_id_asr_output_reg_cfg_t *)ev_reg_payload;
        asr_reg_cfg->event_payload_type = 0;
        event_id = EVENT_ID_ASR_OUTPUT;
        param_id_list.push_back(ASR_INPUT_CONFIG);
        param_id_list.push_back(ASR_INPUT_BUF_DURATON);
        param_id_list.push_back(ASR_OUTPUT_CONFIG);
    } else {
        if (client_id == ASR_CLIENT_HLOS) {
            // update event registration payload for HLOS client only
            ev_reg_size = sizeof(struct event_id_asr_output_reg_cfg_v2_t);
            ev_reg_payload = (uint8_t *)calloc(1, ev_reg_size);
            if (ev_reg_payload == NULL) {
                PAL_ERR(LOG_TAG, "Failed to allocate event payload");
                status = -ENOMEM;
                goto exit;
            }
            asr_reg_v2_cfg =
                (struct event_id_asr_output_reg_cfg_v2_t *)ev_reg_payload;
            asr_reg_v2_cfg->client_id = client_id;
            asr_reg_v2_cfg->event_payload_type = 0;
            event_id = EVENT_ID_ASR_OUTPUT_V2;
        }
        param_id_list.push_back(ASR_INPUT_CONFIG);
        param_id_list.push_back(ASR_INPUT_BUF_DURATON_V2);
        param_id_list.push_back(ASR_OUTPUT_CONFIG_V2);

        outputCfgSize = sizeof(param_id_asr_output_config_v2_t) +
            streamList.size() *
            sizeof(struct param_id_asr_output_config_v2_base_t);
        outputCfg = (param_id_asr_output_config_v2_t *)calloc(1, outputCfgSize);
        if (!outputCfg) {
            PAL_ERR(LOG_TAG, "Failed to allocate output config");
            status = -ENOMEM;
            goto exit;
        }
        outputCfg->num_clients = streamList.size();
        client_configs = (param_id_asr_output_config_v2_base_t *)
            ((uint8_t *)outputCfg + sizeof(param_id_asr_output_config_v2_t));
        for (int i = 0; i < outputCfg->num_clients; i++) {
            client_configs[i].client_id = streamList[i]->GetClientId();
            asr_out_cfg = streamList[i]->GetOutputConfig();
            if (!asr_out_cfg) {
                PAL_ERR(LOG_TAG, "Invalid asr output config");
                status = -EINVAL;
                goto exit;
            }
            client_configs[i].output_mode = asr_out_cfg->output_mode;
            client_configs[i].out_buf_size = asr_out_cfg->out_buf_size;
            client_configs[i].num_bufs = asr_out_cfg->num_bufs;
            if (streamList[i]->GetClientId() == ASR_CLIENT_HLOS) {
                loggerModeEnabled = (asr_out_cfg->output_mode == ASR_LOGGER) ||
                    (asr_out_cfg->output_mode == ASR_TS_LOGGER);
                timestampEnabled = (asr_out_cfg->output_mode == ASR_TS_BUFFERED) ||
                    (asr_out_cfg->output_mode == ASR_TS_NON_BUFFERED) ||
                    (asr_out_cfg->output_mode == ASR_TS_LOGGER);
            }
        }
    }
    if (ev_reg_payload) {
        dynamic_cast<SessionAR*>(session)->setEventPayload(
            event_id, (void *)ev_reg_payload, ev_reg_size);
    }

    for (auto iter: param_id_list) {
        status = setParameters(s, iter);
        if (status) {
            PAL_ERR(LOG_TAG, "Failed to set config %d", iter);
            goto exit;
        }
    }

exit:
    if (ev_reg_payload)
        free(ev_reg_payload);
    if (outputCfg) {
        free(outputCfg);
        outputCfg = nullptr;
    }
    return status;
}

int32_t ASREngine::UpdateSDZConfiguration(StreamASR *s)
{
    int32_t status = 0;
    uint8_t *ev_reg_payload = nullptr;
    size_t ev_reg_size = 0;
    struct event_id_sdz_output_reg_cfg_t *sdz_reg_cfg = nullptr;
    struct event_id_sdz_output_reg_cfg_v2_t *sdz_reg_cfg_v2 = nullptr;
    std::vector<asr_param_id_type_t> param_id_list;
    sdz_client_id_t client_id = SDZ_CLIENT_INVALID;
    param_id_sdz_output_config_t *sdz_out_cfg = nullptr;
    param_id_sdz_output_config_v2_base_t *client_configs = nullptr;
    uint32_t event_id = 0;

    if (!s) {
        PAL_ERR(LOG_TAG, "Invalid stream pointer");
        return -EINVAL;
    }

    client_id = (sdz_client_id_t)s->GetClientId();
    if (client_id == SDZ_CLIENT_INVALID) {
        // update event registration payload
        ev_reg_size = sizeof(struct event_id_sdz_output_reg_cfg_t);
        ev_reg_payload = (uint8_t *)calloc(1, ev_reg_size);
        if (ev_reg_payload == NULL) {
            PAL_ERR(LOG_TAG, "Failed to allocate event payload");
            status = -ENOMEM;
            goto exit;
        }
        sdz_reg_cfg = (struct event_id_sdz_output_reg_cfg_t *)ev_reg_payload;
        sdz_reg_cfg->event_payload_type = 0;
        event_id = EVENT_ID_SDZ_OUTPUT;

        // update input/output configurations
        param_id_list.push_back(SDZ_ENABLE);
        param_id_list.push_back(SDZ_INPUT_BUF_DURATION);
        param_id_list.push_back(SDZ_OUTPUT_CONFIG);
    } else {
        if (client_id == SDZ_CLIENT_HLOS) {
            // update event registration payload
            ev_reg_size = sizeof(struct event_id_sdz_output_reg_cfg_v2_t);
            ev_reg_payload = (uint8_t *)calloc(1, ev_reg_size);
            if (ev_reg_payload == NULL) {
                PAL_ERR(LOG_TAG, "Failed to allocate event payload");
                status = -ENOMEM;
                goto exit;
            }
            sdz_reg_cfg_v2 = (struct event_id_sdz_output_reg_cfg_v2_t *)ev_reg_payload;
            sdz_reg_cfg_v2->client_id = client_id;
            sdz_reg_cfg_v2->event_payload_type = 0;
            event_id = EVENT_ID_SDZ_OUTPUT_V2;
        }
        param_id_list.push_back(SDZ_ENABLE);
        param_id_list.push_back(SDZ_INPUT_BUF_DURATION_V2);
        param_id_list.push_back(SDZ_OUTPUT_CONFIG_V2);

        sdzOutputCfgSize = sizeof(param_id_sdz_output_config_v2_t) +
            streamList.size() *
            sizeof(struct param_id_sdz_output_config_v2_base_t);
        sdzOutputCfg = (param_id_sdz_output_config_v2_t *)calloc(1, sdzOutputCfgSize);
        if (!sdzOutputCfg) {
            PAL_ERR(LOG_TAG, "Failed to allocate sdz output config");
            status = -ENOMEM;
            goto exit;
        }
        sdzOutputCfg->num_clients = streamList.size();
        client_configs = (param_id_sdz_output_config_v2_base_t *)
            ((uint8_t *)sdzOutputCfg + sizeof(param_id_sdz_output_config_v2_t));
        for (int i = 0; i < sdzOutputCfg->num_clients; i++) {
            client_configs[i].client_id = streamList[i]->GetClientId();
            sdz_out_cfg = streamList[i]->GetSdzOutputConfig();
            if (!sdz_out_cfg) {
                PAL_ERR(LOG_TAG, "Invalid sdz output config");
                status = -EINVAL;
                goto exit;
            }
            client_configs[i].output_mode = sdz_out_cfg->output_mode;
            client_configs[i].out_buf_size = sdz_out_cfg->out_buf_size;
            client_configs[i].num_bufs = sdz_out_cfg->num_bufs;
        }
    }

    if (ev_reg_payload) {
        dynamic_cast<SessionAR*>(session)->setEventPayload(
            event_id, (void *)ev_reg_payload, ev_reg_size);
    }

    for (auto iter: param_id_list) {
        status = setParameters(s, iter);
        if (status) {
            PAL_ERR(LOG_TAG, "Failed to set config %d", iter);
            goto exit;
        }
    }

exit:
    if (ev_reg_payload)
        free(ev_reg_payload);
    if (sdzOutputCfg) {
        free(sdzOutputCfg);
        sdzOutputCfg = nullptr;
    }
    return status;
}

int32_t ASREngine::AttachStream(StreamASR *s)
{
    StreamASR *str = nullptr;

    if (!s) {
        PAL_ERR(LOG_TAG, "Invalid stream pointer");
        return -EINVAL;
    }

    if (!speechCfg) {
        speechCfg = (param_id_asr_config_t *)calloc(1,
            sizeof(param_id_asr_config_t));
        if (!speechCfg) {
            PAL_ERR(LOG_TAG, "Failed to allocate memory for asr config");
            return -ENOMEM;
        }
    }

    auto iter = std::find(streamList.begin(), streamList.end(), s);
    if (iter == streamList.end())
        streamList.push_back(s);

    for (int i = 0; i < streamList.size(); i++) {
        str = streamList[i];
        if (str && str->GetClientId() != ASR_CLIENT_LLS)
            break;
    }

    if (str && str->GetSpeechConfig()) {
        memcpy(speechCfg, str->GetSpeechConfig(),
            sizeof(param_id_asr_config_t));
    }

    return 0;
}

int32_t ASREngine::DetachStream(StreamASR *s)
{
    param_id_asr_config_t *asr_config = nullptr;

    auto iter = std::find(streamList.begin(), streamList.end(), s);
    if (iter != streamList.end())
        streamList.erase(iter);

    if (streamList.size() && speechCfg) {
        asr_config = streamList[0]->GetSpeechConfig();
        if (asr_config)
            memcpy(speechCfg, asr_config, sizeof(param_id_asr_config_t));
    } else if (speechCfg) {
        free(speechCfg);
        speechCfg = nullptr;
    }

    return 0;
}

int32_t ASREngine::getCustomParam(
    custom_payload_uc_info_t* uc_info, std::string param_str,
    void* param_payload, size_t* payload_size, Stream *s)
{
    if (!session) {
        PAL_ERR(LOG_TAG, "Invalid session");
        return -EINVAL;
    }

    return session->getCustomParam(
        uc_info, param_str, param_payload, payload_size, s);
}

int32_t ASREngine::setParameters(StreamASR *s, asr_param_id_type_t pid, void *paramPayload)
{
    int32_t status = 0;
    uint32_t tagId = 0;
    uint32_t paramId = 0;
    uint8_t *payload = nullptr;
    uint8_t *data = nullptr;
    size_t payloadSize = 0;
    size_t dataSize = 0;
    uint32_t sesParamId = 0;
    uint32_t miid = 0;
    param_id_asr_input_threshold_v2_t asrInputBufCfg = {};
    param_id_sdz_input_threshold_v2_t sdzInputBufCfg = {};
    param_id_module_enable_t param;

    PAL_DBG(LOG_TAG, "Enter, param id %d ", pid);

    if (pid < ASR_INPUT_CONFIG || pid >= ASR_MAX_PARAM_IDS) {
        PAL_ERR(LOG_TAG, "Invalid param id %d", pid);
        status = -EINVAL;
        goto exit;
    }

    tagId = moduleTagIds[pid];
    paramId = paramIds[pid];

    status = dynamic_cast<SessionAR*>(session)->getMIID(nullptr, tagId, &miid);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "Failed to get instance id for tag %x, status = %d",
                tagId, status);
        goto exit;
    }

    switch (pid) {
        case ASR_INPUT_CONFIG : {
            if (speechCfg == nullptr) {
                PAL_ERR(LOG_TAG, "No config available, can't start the engine!!!");
                goto exit;
            }
            data = (uint8_t *)speechCfg;
            dataSize = sizeof(param_id_asr_config_t);
            sesParamId = PAL_PARAM_ID_ASR_CONFIG;
            break;
        }
        case ASR_ABORT_EVENT : {
            /* Abort event is triggered from stream, we need to push dummy event in event queue
             * and leverage the event handling flow.
             */
            event_id_asr_output_event_t *event = (event_id_asr_output_event_t *)
                                 calloc(1, sizeof(event_id_asr_output_event_t));
            if (event == nullptr) {
                PAL_ERR(LOG_TAG, "Failed to allocate memory for ASR output event");
                goto exit;
            }

            event->event_payload_type = ASR_ABORT_TYPE;
            event->output_token = 0;
            event->num_outputs = 0;
            event->payload_size = 0;
            eventQ.push({EVENT_ID_ASR_OUTPUT, event});
            cv.notify_one();
            break;
        }
        case ASR_FORCE_OUTPUT : {
            if (loggerModeEnabled) {
                /* In logger mode, HLOS will not get any event from DSP, HLOS need to
                 * trigger a getParam to get the transcription, hence following dummy event
                 * is pushed in the event queue, so that getParam and all other handling
                 * can be leveraged by logger mode. Number of output is updated with 1,
                 * and payloadSize with outputBufSize, as these will be used by payload
                 * builder during getParam, as DSP will send payload of output buffer size,
                 * which was used while setting up the usecase.
                 */
                event_id_asr_output_event_t *event = (event_id_asr_output_event_t *)
                                     calloc(1, sizeof(event_id_asr_output_event_t));
                if (event == nullptr) {
                    PAL_ERR(LOG_TAG, "Failed to allocate memory for ASR output event");
                    goto exit;
                }
                event->output_token = 0;
                event->num_outputs = 1;
                event->payload_size = outputBufSize;
                eventQ.push({EVENT_ID_ASR_OUTPUT, event});
                cv.notify_one();
                goto exit;
            }

            param_id_asr_force_output_t *param = (param_id_asr_force_output_t *)
                                    calloc(1, sizeof(param_id_asr_force_output_t));
            if (param == nullptr) {
                 PAL_ERR(LOG_TAG, "Failed to allocate memory for ASR force output config!!!");
                 goto exit;
            }
            param->force_output = 1;
            data = (uint8_t *)param;
            dataSize = sizeof(param_id_asr_force_output_t);
            sesParamId = PAL_PARAM_ID_ASR_FORCE_OUTPUT;
            break;
        }
        case ASR_FORCE_OUTPUT_V2 : {
            if (loggerModeEnabled) {
                /* In logger mode, HLOS will not get any event from DSP, HLOS need to
                 * trigger a getParam to get the transcription, hence following dummy event
                 * is pushed in the event queue, so that getParam and all other handling
                 * can be leveraged by logger mode. Number of output is updated with 1,
                 * and payloadSize with outputBufSize, as these will be used by payload
                 * builder during getParam, as DSP will send payload of output buffer size,
                 * which was used while setting up the usecase.
                 */
                event_id_asr_output_event_t *event = (event_id_asr_output_event_t *)
                                     calloc(1, sizeof(event_id_asr_output_event_t));
                if (event == nullptr) {
                    PAL_ERR(LOG_TAG, "Failed to allocate memory for ASR output event");
                    goto exit;
                }
                event->output_token = 0;
                event->num_outputs = 1;
                event->payload_size = outputBufSize;
                eventQ.push({EVENT_ID_ASR_OUTPUT_V2, event});
                cv.notify_one();
                goto exit;
            }

            param_id_asr_force_output_v2_t *param = (param_id_asr_force_output_v2_t *)
                                    calloc(1, sizeof(param_id_asr_force_output_v2_t));
            if (param == nullptr) {
                 PAL_ERR(LOG_TAG, "Failed to allocate memory for ASR force output config!!!");
                 goto exit;
            }
            param->client_id = s->GetClientId();
            param->force_output = 1;
            data = (uint8_t *)param;
            dataSize = sizeof(param_id_asr_force_output_v2_t);
            sesParamId = PAL_PARAM_ID_ASR_FORCE_OUTPUT;
            break;
        }
        case SDZ_FORCE_OUTPUT : {
            param_id_sdz_force_output_t *param = (param_id_sdz_force_output_t *)
                                calloc(1, sizeof(param_id_sdz_force_output_t));
            if (param == nullptr) {
                PAL_ERR(LOG_TAG, "Failed to allocate memory for SDZ force output config!!!");
                goto exit;
            }
            param->force_output = 1;
            data = (uint8_t *)param;
            dataSize = sizeof(param_id_sdz_force_output_t);
            sesParamId = PAL_PARAM_ID_SDZ_FORCE_OUTPUT;
            break;
        }
        case SDZ_FORCE_OUTPUT_V2 : {
            param_id_sdz_force_output_v2_t *param = (param_id_sdz_force_output_v2_t *)
                                calloc(1, sizeof(param_id_sdz_force_output_v2_t));
            if (param == nullptr) {
                PAL_ERR(LOG_TAG, "Failed to allocate memory for SDZ force output config!!!");
                goto exit;
            }
            param->client_id = s->GetClientId();
            param->force_output = 1;
            data = (uint8_t *)param;
            dataSize = sizeof(param_id_sdz_force_output_v2_t);
            sesParamId = PAL_PARAM_ID_SDZ_FORCE_OUTPUT;
            break;
        }
        case ASR_OUTPUT_CONFIG : {
            param_id_asr_output_config_t *opConfig = s->GetOutputConfig();
            if (opConfig == nullptr) {
                PAL_ERR(LOG_TAG, "No output config available, can't start the engine!!!");
                goto exit;
            }
            if (opConfig->output_mode == ASR_LOGGER || opConfig->output_mode == ASR_TS_LOGGER)
                loggerModeEnabled = true;
            if (opConfig->output_mode == ASR_TS_BUFFERED ||
                opConfig->output_mode == ASR_TS_NON_BUFFERED ||
                opConfig->output_mode == ASR_TS_LOGGER)
                timestampEnabled = true;
            outputBufSize = opConfig->out_buf_size;
            data = (uint8_t *)opConfig;
            dataSize = sizeof(param_id_asr_output_config_t);
            sesParamId = PAL_PARAM_ID_ASR_OUTPUT;
            break;
        }
        case ASR_OUTPUT_CONFIG_V2 : {
            if (outputCfg == nullptr || outputCfgSize == 0) {
                PAL_ERR(LOG_TAG, "No output config available, can't start the engine!!!");
                goto exit;
            }
            data = (uint8_t *)outputCfg;
            dataSize = outputCfgSize;
            sesParamId = PAL_PARAM_ID_ASR_OUTPUT;
            break;
        }
        case SDZ_OUTPUT_CONFIG : {
            param_id_sdz_output_config_t *opConfigSdz = s->GetSdzOutputConfig();
            if (opConfigSdz == nullptr) {
                PAL_ERR(LOG_TAG, "No output config available for Sdz!!!");
                goto exit;
            }
            data = (uint8_t *)opConfigSdz;
            dataSize = sizeof(param_id_sdz_output_config_t);
            sesParamId = PAL_PARAM_ID_SDZ_OUTPUT;
            break;
        }
        case SDZ_OUTPUT_CONFIG_V2 : {
            if (sdzOutputCfg == nullptr || sdzOutputCfgSize == 0) {
                PAL_ERR(LOG_TAG, "No output config available, can't start the engine!!!");
                goto exit;
            }
            data = (uint8_t *)sdzOutputCfg;
            dataSize = sdzOutputCfgSize;
            sesParamId = PAL_PARAM_ID_SDZ_OUTPUT;
            break;
        }
        case ASR_INPUT_BUF_DURATON: {
            param_id_asr_input_threshold_t *ipConfig = s->GetInputBufConfig();
            if (ipConfig == nullptr) {
                PAL_ERR(LOG_TAG, "No input config available, can't start the engine!!!");
                goto exit;
            }
            data = (uint8_t *)ipConfig;
            dataSize = sizeof(param_id_asr_input_threshold_t);
            sesParamId = PAL_PARAM_ID_ASR_SET_PARAM;
            break;
        }
        case ASR_INPUT_BUF_DURATON_V2: {
            param_id_asr_input_threshold_t *ipConfig = s->GetInputBufConfig();
            if (ipConfig == nullptr) {
                PAL_ERR(LOG_TAG, "No input config available, can't start the engine!!!");
                goto exit;
            }
            asrInputBufCfg.client_id = s->GetClientId();
            asrInputBufCfg.buf_duration_ms = ipConfig->buf_duration_ms;
            data = (uint8_t *)&asrInputBufCfg;
            dataSize = sizeof(param_id_asr_input_threshold_v2_t);
            sesParamId = PAL_PARAM_ID_ASR_SET_PARAM;
            break;
        }
        case SDZ_INPUT_BUF_DURATION: {
            param_id_sdz_input_threshold_t *ipConfigSdz = s->GetSdzInputBufferConfig();
            if (ipConfigSdz == nullptr) {
                PAL_ERR(LOG_TAG, "No input config available for Sdz!!!");
                goto exit;
            }
            data = (uint8_t *)ipConfigSdz;
            dataSize = sizeof(param_id_sdz_input_threshold_t);
            sesParamId = PAL_PARAM_ID_SDZ_SET_PARAM;
            break;
        }
        case SDZ_INPUT_BUF_DURATION_V2: {
            param_id_sdz_input_threshold_t *ipConfigSdz = s->GetSdzInputBufferConfig();
            if (ipConfigSdz == nullptr) {
                PAL_ERR(LOG_TAG, "No input config available for Sdz!!!");
                goto exit;
            }
            sdzInputBufCfg.client_id = s->GetClientId();
            sdzInputBufCfg.buf_duration_ms = ipConfigSdz->buf_duration_ms;
            data = (uint8_t *)&sdzInputBufCfg;
            dataSize = sizeof(param_id_sdz_input_threshold_v2_t);
            sesParamId = PAL_PARAM_ID_SDZ_SET_PARAM;
            break;
        }
        case SDZ_ENABLE : {
            param.enable = 1;
            data = (uint8_t *)&param;
            dataSize = sizeof(param_id_module_enable_t);
            sesParamId = PAL_PARAM_ID_SDZ_ENABLE;
            break;
        }
        default : {
            PAL_ERR(LOG_TAG, "Unexpected param ID is sent, not implemented yet");
        }
    }

    status = builder->payloadConfig(&payload, &payloadSize, data, dataSize,
                                        miid, paramId);
    if (status || !payload) {
        PAL_ERR(LOG_TAG, "Failed to construct ASR payload, status = %d",
            status);
        return -ENOMEM;
    }

    status = dynamic_cast<SessionAR*>(session)->setParamWithTag(s, tagId,
                                                                sesParamId, payload);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "Failed to set payload for param id %x, status = %d",
            sesParamId, status);
    }

exit:
    if (data != NULL && (pid == ASR_FORCE_OUTPUT || pid == SDZ_FORCE_OUTPUT)) {
        free(data);
        data = nullptr;
    }

    PAL_DBG(LOG_TAG, "Exit, status %d", status);
    return status;
}

int32_t ASREngine::StartEngine(StreamASR *s)
{
    PAL_DBG(LOG_TAG, "Enter");

    int32_t status = 0;
    int32_t tempStatus = 0;

    std::lock_guard<std::mutex> lck(mutexEngine);

    if (IsEngineActive()) {
        status = session->stop(streamList[0]);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "Error:%d Failed to stop session", status);
            goto exit;
        }
    } else {
        status = session->open(s);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "Error:%d Failed to open session", status);
            goto exit;
        }
    }

    status = AttachStream(s);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "Failed to attach stream, status = %d", status);
        goto err_cleanup;
    }

    status = UpdateASRConfiguration(s);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "Failed to update ASR config, status = %d", status);
        goto err_cleanup;
    }

    if (s->EnableSpeakerDiarization()) {
        status = UpdateSDZConfiguration(s);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "Failed to update SDZ config, status = %d", status);
            goto err_cleanup;
        }
    }

    status = session->prepare(s);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "Error:%d Failed to prepare session", status);
        goto err_cleanup;
    }

    status = session->start(s);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "Error:%d Failed to start session", status);
        goto err_cleanup;
    }

    engState = ASR_ENG_ACTIVE;
    goto exit;

err_cleanup:
    tempStatus = session->close(s);
    if (tempStatus)
        PAL_ERR(LOG_TAG, "Error: %d Failed to close session", tempStatus);

exit:
    PAL_DBG(LOG_TAG, "Exit, status %d", status);
    return status;
}

int32_t ASREngine::StopEngine(StreamASR *s)
{
    int32_t status = 0;

    PAL_DBG(LOG_TAG, "Enter");
    std::lock_guard<std::mutex> lck(mutexEngine);

    status = session->stop(s);
    if (status) {
        PAL_ERR(LOG_TAG, "Error:%d Failed to stop session", status);
    }

    DetachStream(s);

    // clear event payload list when stopping hlos client
    if (s->GetClientId() == ASR_CLIENT_HLOS)
        dynamic_cast<SessionAR*>(session)->clearEventPayloadList();

    if (streamList.size()) {
        status = UpdateASRConfiguration(streamList[0]);
        if (status) {
            PAL_ERR(LOG_TAG, "Failed to update ASR config, status = %d", status);
            goto exit;
        }

        if (streamList[0]->EnableSpeakerDiarization()) {
            status = UpdateSDZConfiguration(streamList[0]);
            if (0 != status) {
                PAL_ERR(LOG_TAG, "Failed to update SDZ config, status = %d", status);
                goto exit;
            }
        }
        status = session->start(streamList[0]);
        if (status)
            PAL_ERR(LOG_TAG, "Error: %d Failed to close session", status);
    } else {
        status = session->close(s);
        if (status)
            PAL_ERR(LOG_TAG, "Error: %d Failed to close session", status);

        engState = ASR_ENG_IDLE;
        loggerModeEnabled = false;
        timestampEnabled = false;
    }

exit:
    PAL_DBG(LOG_TAG, "Exit, status = %d", status);
    return status;
}

void ASREngine::ParseEventAndNotifyStream(void* eventData) {

    PAL_DBG(LOG_TAG, "Enter.");

    int32_t status = 0;
    bool abortEvent = false;
    void *payload = nullptr;
    uint8_t *temp = nullptr;
    event_id_asr_output_event_t *event = nullptr;
    asr_output_status_t *ev = nullptr;
    param_id_asr_output_t *eventHeader = nullptr;
    eventPayload eventToStream {};
    StreamASR *sAsr = nullptr;
    uint32_t paramId = 0;

    for (int i = 0; i < streamList.size(); i++) {
        if (streamList[i] &&
            streamList[i]->GetClientId() != ASR_CLIENT_LLS) {
            sAsr = streamList[i];
            break;
        }
    }
    if (!sAsr) {
        PAL_ERR(LOG_TAG, "No matching ASR stream");
        goto exit;
    }

    event = (struct event_id_asr_output_event_t *)eventData;
    if (event == nullptr) {
        PAL_ERR(LOG_TAG, "Invalid event!!!");
        goto exit;
    }

    abortEvent = event->event_payload_type == ASR_ABORT_TYPE;

    PAL_INFO(LOG_TAG, "Logger mode : %d, Output mode : %d, output token : %d, num output : %d, payload size : %d",
            loggerModeEnabled, event->event_payload_type, event->output_token, event->num_outputs, event->payload_size);

    if (event->num_outputs == 0) {
        PAL_ERR(LOG_TAG, "event raised without any transcript");
        goto cleanup;
    }

    /**
     * Don't move following variable updates after the getParam, as these variables
     * will be used by payload builder while handling the getParam.
     */
    numOutput = event->num_outputs;
    outputToken = event->output_token;
    payloadSize = event->payload_size;

    paramId = (sAsr->GetClientId() == ASR_CLIENT_HLOS) ?
        PAL_PARAM_ID_MULTI_CLIENT_ASR_OUTPUT : PAL_PARAM_ID_ASR_OUTPUT;
    if (!abortEvent) {
        status = dynamic_cast<SessionAR*>(session)->getParamWithTag(sAsr,
                           moduleTagIds[ASR_OUTPUT], paramId, &payload);
        if (status != 0) {
            PAL_ERR(LOG_TAG, "Failed to get output payload");
            goto cleanup;
        }
    } else {
        payload = calloc(1, sizeof(struct param_id_asr_output_t));
        if (!payload) {
            PAL_ERR(LOG_TAG, "Failed to allocate memory for payload");
            goto cleanup;
        }
    }

    numOutput = 0;
    outputToken = 0;
    payloadSize = 0;

    temp = (uint8_t *)payload;
    if (sAsr->GetClientId() == ASR_CLIENT_HLOS && !abortEvent) {
        if (*(uint32_t *)temp != ASR_CLIENT_HLOS) {
            PAL_ERR(LOG_TAG, "Invalid client id");
            goto cleanup;
        }
        temp += sizeof(uint32_t);
    }

    eventHeader = (struct param_id_asr_output_t *)temp;
    if (timestampEnabled) {
        PAL_INFO(LOG_TAG, "Timestamp based event received");
        asr_output_status_v2_t *ev = (asr_output_status_v2_t *)(temp + sizeof(struct param_id_asr_output_t));
        pal_asr_ts_event *eventPayload = nullptr;
        asr_word_status_t *words = nullptr;

        eventToStream.type = TIMESTAMP_BASED_TEXT;
        eventToStream.payloadSize = sizeof(pal_asr_ts_event) + eventHeader->num_outputs *
                                    sizeof(pal_asr_engine_ts_event);
        eventToStream.payload = calloc(1, eventToStream.payloadSize);
        if (eventToStream.payload == nullptr) {
            PAL_ERR(LOG_TAG, "Failed to allocate memory for stream event!!!");
            goto cleanup;
        }
        eventPayload = (pal_asr_ts_event *)eventToStream.payload;
        eventPayload->num_events = eventHeader->num_outputs;
        eventPayload->status = abortEvent ? PAL_ASR_EVENT_STATUS_ABORTED :
                                            PAL_ASR_EVENT_STATUS_SUCCESS;

        for (int i = 0; i < eventHeader->num_outputs; i++) {
            if (ev->status == ASR_FAIL) {
                PAL_INFO(LOG_TAG, "Received failure event, ignoring this event!!!");
                goto cleanup;
            } else if (ev->num_words >= MAX_NUM_WORDS) {
                PAL_INFO(LOG_TAG, "Received event, with more words %d than allowed %d!!!",
                         ev->num_words, MAX_NUM_WORDS);
                goto cleanup;
            }

            eventPayload->status = ev->status == ASR_TIMEOUT ? PAL_ASR_EVENT_STATUS_TIMEOUT :
                                                        PAL_ASR_EVENT_STATUS_SUCCESS;
            eventPayload->event[i].is_final = ev->is_final;
            eventPayload->event[i].confidence = ev->confidence;
            eventPayload->event[i].text_size = ev->text_size < 0 ? 0 : ev->text_size;
            eventPayload->event[i].start_ts = ((uint64_t)ev->segment_start_time_ms_msw << 32 |
                                               (uint64_t)ev->segment_start_time_ms_lsw);
            eventPayload->event[i].end_ts = ((uint64_t)ev->segment_end_time_ms_msw << 32 |
                                             (uint64_t)ev->segment_end_time_ms_lsw);
            eventPayload->event[i].num_words = ev->num_words;
            words = (asr_word_status_t *)(((uint8_t *)ev) + sizeof(asr_output_status_v2_t));
            for (int j = 0; j < eventPayload->event[i].text_size; ++j)
                eventPayload->event[i].text[j] = ev->text[j];
            for (int j = 0; j < ev->num_words; j++) {
                eventPayload->event[i].word[j].word_confidence = words[j].word_confidence;
                eventPayload->event[i].word[j].start_ts = ((uint64_t)words[j].word_start_time_ms_msw << 32 |
                                                          (uint64_t)words[j].word_start_time_ms_lsw);
                eventPayload->event[i].word[j].end_ts = ((uint64_t)words[j].word_end_time_ms_msw << 32 |
                                                        (uint64_t)words[j].word_end_time_ms_lsw);
                for (int k = 0; k < words[j].word_size; k++)
                    eventPayload->event[i].word[j].word[k] = words[j].word[k];
            }
            if (i < eventHeader->num_outputs - 1)
                ev = (asr_output_status_v2_t *)((uint8_t *)ev + sizeof(asr_output_status_v2_t) +
                                                 ev->num_words * sizeof(asr_word_status_t));
        }
    } else {
        asr_output_status_t *ev = (asr_output_status_t *)(temp + sizeof(struct param_id_asr_output_t));
        pal_asr_event *eventPayload = nullptr;

        eventToStream.type = PLAIN_TEXT;
        eventToStream.payloadSize = sizeof(pal_asr_event) + eventHeader->num_outputs * sizeof(pal_asr_engine_event);

        eventToStream.payload = calloc(1, eventToStream.payloadSize);
        if (eventToStream.payload == nullptr) {
            PAL_ERR(LOG_TAG, "Failed to allocate memory for stream event!!!");
            return;
        }
        eventPayload = (pal_asr_event *)eventToStream.payload;
        eventPayload->num_events = eventHeader->num_outputs;
        eventPayload->status = abortEvent ? PAL_ASR_EVENT_STATUS_ABORTED :
                                            PAL_ASR_EVENT_STATUS_SUCCESS;

        for (int i = 0; i < eventHeader->num_outputs; i++) {
            if (ev[i].status == ASR_FAIL) {
                PAL_INFO(LOG_TAG, "Received failure event, ignoring this event!!!");
                goto cleanup;
            }
            eventPayload->status = ev[i].status == ASR_TIMEOUT ? PAL_ASR_EVENT_STATUS_TIMEOUT :
                                                        PAL_ASR_EVENT_STATUS_SUCCESS;
            eventPayload->event[i].is_final = ev[i].is_final;
            eventPayload->event[i].confidence = ev[i].confidence;
            eventPayload->event[i].text_size = ev[i].text_size < 0 ? 0 : ev[i].text_size;
            for (int j = 0; j < ev[i].text_size; ++j)
                eventPayload->event[i].text[j] = ev[i].text[j];
        }
    }

    mutexEngine.unlock();
    sAsr->HandleEventData(eventToStream);
    mutexEngine.lock();

cleanup:
    if (eventToStream.payload) {
        free(eventToStream.payload);
        eventToStream.payload = nullptr;
    }

    if (payload) {
        free(payload);
        payload = nullptr;
    }

    if (eventData) {
        event = nullptr;
        free(eventData);
        eventData = nullptr;
    }

exit:
    PAL_DBG(LOG_TAG, "Exit.");
}

void ASREngine::ParseSdzEventAndNotifyStream(void* eventData) {

    PAL_DBG(LOG_TAG, "Enter.");

    int32_t status = 0;
    bool overlapDetected = true;
    void *payload = nullptr;
    uint8_t *temp = nullptr;
    uint32_t numSpeakers = 0;
    std::vector<std::vector<struct sdz_speaker_info>> sdzOutputVector;
    std::vector<std::pair<bool, uint32_t>> sdzOverlapNumSpeakerVector;
    event_id_sdz_output_event_t *event = nullptr;
    event_id_sdz_output_event_v2_t *eventV2 = nullptr;
    sdz_output_status_t *ev = nullptr;
    sdz_output_status_v2_t *evV2 = nullptr;
    param_id_sdz_output_t *eventHeader = nullptr;
    param_id_sdz_output_v2_t *eventHeaderV2 = nullptr;
    sdz_speaker_info_t *eventSpeakerInfo = nullptr;
    sdz_speaker_segment_t *eventSpeakerSegment = nullptr;
    pal_sdz_event *sdzEventPayload = nullptr;
    eventPayload eventToStream;
    StreamASR *sAsr = nullptr;
    uint32_t paramId = 0;

    for (int i = 0; i < streamList.size(); i++) {
        if (streamList[i] &&
            streamList[i]->GetClientId() != ASR_CLIENT_LLS) {
            sAsr = streamList[i];
            break;
        }
    }
    if (!sAsr) {
        PAL_ERR(LOG_TAG, "No matching ASR stream");
        goto exit;
    }

    if (sAsr->GetClientId() == SDZ_CLIENT_INVALID) {
        event = (event_id_sdz_output_event_t *)eventData;
    } else {
        eventV2 = (event_id_sdz_output_event_v2_t *)eventData;
        event = (event_id_sdz_output_event_t *)&eventV2->event_payload_type;
    }
    if (event == nullptr) {
        PAL_ERR(LOG_TAG, "Invalid event!!!");
        goto exit;
    }

    PAL_INFO(LOG_TAG, "Output mode : %d, output token : %d, num output : %d, payload size : %d",
        event->event_payload_type, event->output_token, event->num_outputs, event->payload_size);

    if (event->num_outputs == 0) {
        PAL_ERR(LOG_TAG, "event raised without any speaker info");
        goto cleanup;
    }

    sdzNumOutput = event->num_outputs;
    sdzOutputToken = event->output_token;
    if (sAsr->GetClientId() == SDZ_CLIENT_HLOS) {
        sdzPayloadSize = sizeof(param_id_sdz_output_v2_t) + event->payload_size;
        paramId = PAL_PARAM_ID_MULTI_CLIENT_SDZ_OUTPUT;
    } else {
        sdzPayloadSize = sizeof(param_id_sdz_output_t) + event->payload_size;
        paramId = PAL_PARAM_ID_SDZ_OUTPUT;
    }

    status = dynamic_cast<SessionAR*>(session)->getParamWithTag(sAsr,
                           moduleTagIds[SDZ_OUTPUT], paramId, &payload);
    if (status != 0) {
        PAL_ERR(LOG_TAG, "Failed to get output payload");
        goto cleanup;
    }

    if (sAsr->GetClientId() == SDZ_CLIENT_HLOS) {
        eventHeaderV2 = (param_id_sdz_output_v2_t *)payload;
        if (eventHeaderV2->client_id != SDZ_CLIENT_HLOS) {
            PAL_ERR(LOG_TAG, "Invalid client id");
            goto cleanup;
        }

        evV2 = (sdz_output_status_v2_t *)((uint8_t *)payload + sizeof(param_id_sdz_output_v2_t));
        for (int i = 0; i < eventHeaderV2->num_outputs; i++) {
            std::vector<struct sdz_speaker_info> speakerInfoVector;
            eventSpeakerSegment = (sdz_speaker_segment_t *)((uint8_t *)evV2 + sizeof(sdz_output_status_v2_t));
            numSpeakers += evV2->num_segments;
            for (int j = 0; j < evV2->num_segments; j++) {
                struct sdz_speaker_info speakerInfo;
                speakerInfo.speaker_id = eventSpeakerSegment->speaker_id;
                speakerInfo.start_ts = ((uint64_t)eventSpeakerSegment->time_stamp_start_msw << 32 |
                                        (uint64_t)eventSpeakerSegment->time_stamp_start_lsw);
                speakerInfo.end_ts = ((uint64_t)eventSpeakerSegment->time_stamp_end_msw << 32 |
                                        (uint64_t)eventSpeakerSegment->time_stamp_end_lsw);
                speakerInfoVector.push_back(speakerInfo);
                eventSpeakerSegment++;
                temp = (uint8_t *)eventSpeakerSegment;
            }
            sdzOutputVector.push_back(speakerInfoVector);
            uint32_t numSegments = evV2->num_segments;
            sdzOverlapNumSpeakerVector.push_back({numSegments > 1, numSegments});
            evV2 = (sdz_output_status_v2_t *)temp;
        }
        eventToStream.payloadSize = sizeof(pal_sdz_event) +
            sdzOverlapNumSpeakerVector.size() * sizeof(struct sdz_output) +
            numSpeakers * sizeof(sdz_speaker_segment_t);
    } else {
        eventHeader = (param_id_sdz_output_t *)payload;

        ev = (sdz_output_status_t *)((uint8_t *)payload + sizeof(param_id_sdz_output_t));
        for (int i = 0; i < eventHeader->num_outputs; i++) {
            std::vector<struct sdz_speaker_info> speakerInfoVector;
            eventSpeakerInfo = (sdz_speaker_info_t *)((uint8_t *)ev + sizeof(sdz_output_status_t));
            numSpeakers += ev->num_speakers;
            for (int j = 0; j < ev->num_speakers; j++) {
                struct sdz_speaker_info speakerInfo;
                speakerInfo.speaker_id = eventSpeakerInfo->speaker_id;
                speakerInfo.start_ts = ((uint64_t)eventSpeakerInfo->start_ts_msw << 32 |
                                        (uint64_t)eventSpeakerInfo->start_ts_lsw);
                speakerInfo.end_ts = ((uint64_t)eventSpeakerInfo->end_ts_msw << 32 |
                                        (uint64_t)eventSpeakerInfo->end_ts_lsw);
                speakerInfoVector.push_back(speakerInfo);
                eventSpeakerInfo++;
                temp = (uint8_t *)eventSpeakerInfo;
            }
            sdzOutputVector.push_back(speakerInfoVector);
            uint32_t overlapDetectedVal = ev->overlap_detected;
            uint32_t numSpeakersVal = ev->num_speakers;
            sdzOverlapNumSpeakerVector.push_back({overlapDetectedVal, numSpeakersVal});
            ev = (sdz_output_status_t *)temp;
        }
        eventToStream.payloadSize = sizeof(pal_sdz_event) +
            sdzOverlapNumSpeakerVector.size() * sizeof(struct sdz_output) +
            numSpeakers * sizeof(sdz_speaker_info);
    }

    eventToStream.type = SPEAKER_DIARIZATION;
    eventToStream.payload = (void *)calloc(1, eventToStream.payloadSize);
    if (eventToStream.payload == nullptr) {
        PAL_ERR(LOG_TAG, "Failed to allocate memory for stream event!!");
        goto cleanup;
    }

    sdzEventPayload = (pal_sdz_event *)eventToStream.payload;
    sdzEventPayload->num_outputs = sdzOutputVector.size();
    for (int i = 0; i < sdzEventPayload->num_outputs; i++) {
        sdzEventPayload->output[i].overlap_detected = sdzOverlapNumSpeakerVector[i].first;
        sdzEventPayload->output[i].num_speakers = sdzOverlapNumSpeakerVector[i].second;

        for (int j = 0; j < sdzEventPayload->output[i].num_speakers; j++) {
            sdzEventPayload->output[i].speakers_list[j].speaker_id = sdzOutputVector[i][j].speaker_id;
            sdzEventPayload->output[i].speakers_list[j].start_ts = sdzOutputVector[i][j].start_ts;
            sdzEventPayload->output[i].speakers_list[j].end_ts = sdzOutputVector[i][j].end_ts;
        }
    }
    sdzOutputVector.clear();
    sdzOverlapNumSpeakerVector.clear();

    PAL_INFO(LOG_TAG, "Total number of speakers : %d", numSpeakers)

    mutexEngine.unlock();
    sAsr->HandleEventData(eventToStream);
    mutexEngine.lock();

cleanup:
    if (eventToStream.payload) {
        free(eventToStream.payload);
        eventToStream.payload = nullptr;
    }
    if (payload) {
        free(payload);
        payload = nullptr;
    }
    if (eventData) {
        free(eventData);
        event = nullptr;
        eventData = nullptr;
    }
exit:
    PAL_DBG(LOG_TAG, "Exit.");
}

void ASREngine::EventProcessingThread(ASREngine *engine)
{
    PAL_INFO(LOG_TAG, "Enter. start thread loop");
    if (!engine) {
        PAL_ERR(LOG_TAG, "Error:%d Invalid engine", -EINVAL);
        return;
    }
    std::pair<uint32_t, void *> event;
    std::unique_lock<std::mutex> lck(engine->mutexEngine);
    while (!engine->exitThread) {
        while (engine->eventQ.empty()) {
            PAL_DBG(LOG_TAG, "waiting on cond");
            engine->cv.wait(lck);
            PAL_DBG(LOG_TAG, "done waiting on cond");

            if (engine->exitThread) {
                PAL_VERBOSE(LOG_TAG, "Exit thread");
                break;
            }
        }
        //Adding this condition, as destructor can also notify this thread without any event
        if (!engine->eventQ.empty()) {
            event = engine->eventQ.front();
            engine->eventQ.pop();
            if (event.first == EVENT_ID_SDZ_OUTPUT ||
                event.first == EVENT_ID_SDZ_OUTPUT_V2) {
                engine->ParseSdzEventAndNotifyStream(event.second);
            } else {
                engine->ParseEventAndNotifyStream(event.second);
            }
        }
    }

    PAL_INFO(LOG_TAG, "Exit");
}

void ASREngine::HandleSessionEvent(uint32_t eventId,
                                   void *data, uint32_t size)
{
    void *eventData = nullptr;

    std::unique_lock<std::mutex> lck(mutexEngine);

    if (engState == ASR_ENG_IDLE) {
        PAL_INFO(LOG_TAG, "Engine not active, ignore");
        lck.unlock();
        return;
    }

    eventData = calloc(1, size);
    if (!eventData) {
        PAL_ERR(LOG_TAG, "Error:failed to allocate mem for event_data");
        return;
    }

    memcpy(eventData, data, size);
    eventQ.push({eventId, eventData});
    cv.notify_one();
}

void ASREngine::HandleSessionCallBack(uint64_t hdl, uint32_t eventId,
                                      void *data, uint32_t eventSize)
{
    ASREngine *engine = nullptr;

    PAL_INFO(LOG_TAG, "Enter, event detected on SPF, event id = 0x%x", eventId);
    if ((hdl == 0) || !data) {
        PAL_ERR(LOG_TAG, "Error:%d Invalid engine handle or event data", -EINVAL);
        return;
    }

    if (eventId != EVENT_ID_ASR_OUTPUT &&
        eventId != EVENT_ID_ASR_OUTPUT_V2 &&
        eventId != EVENT_ID_SDZ_OUTPUT &&
        eventId != EVENT_ID_SDZ_OUTPUT_V2)
        return;

    engine = (ASREngine *)hdl;
    engine->HandleSessionEvent(eventId, data, eventSize);

    PAL_DBG(LOG_TAG, "Exit");
    return;
}

int32_t ASREngine::setECRef(StreamASR *s, std::shared_ptr<Device> dev, bool isEnable,
                                        bool setECForFirstTime) {
    int32_t status = 0;
    bool forceEnable = false;
    bool isDevEnabledExtEc = false;

    if (!session) {
        PAL_ERR(LOG_TAG, "Invalid session");
        return -EINVAL;
    }
    PAL_DBG(LOG_TAG, "Enter, EC ref count : %d, enable : %d", ecRefCount, isEnable);
    PAL_DBG(LOG_TAG, "Rx device : %s, stream is setting EC for first time : %d",
            dev ? dev->getPALDeviceName().c_str() :  "Null", setECForFirstTime);

    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();
    if (!rm) {
        PAL_ERR(LOG_TAG, "Failed to get resource manager instance");
        return -EINVAL;
    }

    if (dev)
        isDevEnabledExtEc = rm->isExternalECRefEnabled(dev->getSndDeviceId());
    std::unique_lock<std::recursive_mutex> lck(ecRefMutex);
    if (isEnable) {
        if (isCrrDevUsingExtEc && !isDevEnabledExtEc) {
            PAL_ERR(LOG_TAG, "Internal EC cannot be set, when external EC is active");
            return -EINVAL;
        }
        if (setECForFirstTime) {
            ecRefCount++;
        } else if (rxEcDev != dev) {
            forceEnable = true;
        } else {
            return status;
        }
        if (forceEnable || ecRefCount == 1) {
            status = session->setECRef(s, dev, isEnable);
            if (status) {
                PAL_ERR(LOG_TAG, "Failed to set EC Ref for rx device %s",
                        dev ?  dev->getPALDeviceName().c_str() : "Null");
                if (setECForFirstTime) {
                    ecRefCount--;
                }
                if (forceEnable || ecRefCount == 0) {
                    rxEcDev = nullptr;
                }
            } else {
                isCrrDevUsingExtEc = isDevEnabledExtEc;
                rxEcDev = dev;
            }
        }
    } else {
        if (!dev || dev == rxEcDev) {
            if (ecRefCount > 0) {
                ecRefCount--;
                if (ecRefCount == 0) {
                    status = session->setECRef(s, dev, isEnable);
                    if (status) {
                        PAL_ERR(LOG_TAG, "Failed to reset EC Ref");
                    } else {
                        rxEcDev = nullptr;
                        isCrrDevUsingExtEc = false;
                    }
                }
            } else {
                PAL_DBG(LOG_TAG, "Skipping EC disable, as ref count is 0");
            }
        } else {
            PAL_DBG(LOG_TAG, "Skipping EC disable, as EC disable is not for correct device");
        }
    }
    PAL_DBG(LOG_TAG, "Exit, EC ref count : %d", ecRefCount);

    return status;
}

int32_t ASREngine::ConnectSessionDevice(
    StreamASR *streamHandle, pal_stream_type_t streamType,
    std::shared_ptr<Device> deviceToConnect)
{
    PAL_DBG(LOG_TAG, "Enter, devDisconnectCount: %d", devDisconnectCount);
    int32_t status = 0;

    if (!session) {
        PAL_ERR(LOG_TAG, "Invalid session");
        return -EINVAL;
    }

    if (devDisconnectCount == 0)
        status = session->connectSessionDevice(streamHandle, streamType,
                                            deviceToConnect);
    if (status != 0)
        devDisconnectCount++;

    PAL_DBG(LOG_TAG, "Exit, devDisconnectCount: %d", devDisconnectCount);
    return status;
}

int32_t ASREngine::DisconnectSessionDevice(
    StreamASR *streamHandle, pal_stream_type_t streamType,
    std::shared_ptr<Device> deviceToDisconnect)
{
    PAL_DBG(LOG_TAG, "Enter, devDisconnectCount: %d", devDisconnectCount);
    int32_t status = 0;

    if (!session) {
        PAL_ERR(LOG_TAG, "Invalid session");
        return -EINVAL;
    }

    devDisconnectCount++;
    status = session->disconnectSessionDevice(streamHandle, streamType,
                                               deviceToDisconnect);
    if (status != 0)
        devDisconnectCount--;
    PAL_DBG(LOG_TAG, "Exit, devDisconnectCount: %d", devDisconnectCount);
    return status;
}

int32_t ASREngine::SetupSessionDevice(
    StreamASR *streamHandle, pal_stream_type_t streamType,
    std::shared_ptr<Device> deviceToDisconnect)
{
    PAL_DBG(LOG_TAG, "Enter, devDisconnectCount: %d", devDisconnectCount);
    int32_t status = 0;

    if (!session) {
        PAL_ERR(LOG_TAG, "Invalid session");
        return -EINVAL;
    }

    devDisconnectCount--;
    if (devDisconnectCount < 0)
        devDisconnectCount = 0;

    if (devDisconnectCount == 0)
        status = session->setupSessionDevice(streamHandle, streamType,
                                          deviceToDisconnect);
    if (status != 0)
        devDisconnectCount++;

    PAL_DBG(LOG_TAG, "Enter, devDisconnectCount: %d", devDisconnectCount);
    return status;
}
