#include "ffmpeg4.h"

namespace ffmpeg4 {

    int32_t FFmpeg::Do(FFmpegOpenParam1& param)
    {
        int ret = -1;
        bool ok = true;

        do
        {
            ret = avformat_open_input(&pFormatCtx, param.filename.data(), param.fmt, &param.option);
            if (ret < 0) {
                Util::logf(ret, "avformat_open_input fail. %s", param.filename.data());
                break;
            }

            // Retrieve stream information
            ret = avformat_find_stream_info(pFormatCtx, NULL);
            if (ret < 0) {
                Util::logf(ret, "couldn't find stream information.");
                break;
            }

            av_dump_format(pFormatCtx, 0, param.filename.data(), false);

            if (param.OnOpen) {
                ret = param.OnOpen(pFormatCtx);
                if (!ret) { break; }
            }

            // ����
            enableDecode = param.decodeStreamIndexs.size() > 0;

            // �ز���
            enableSWX = param.swxs.size() > 0;

            // ����
            enableEncode = param.encodes.size() > 0;

            // ���� 
            if (!init_decode(param)) {
                break;
            }

            // ת��
            if (!init_sw(param)) {
                break;
            }

            // ����
            if (!init_encode(param)) {
                break;
            }


            packet = av_packet_alloc();
            if (!packet) {
                Util::logf("av_packet_alloc fail.");
                break;
            }

            do
            {
                ret = av_read_frame(pFormatCtx, packet);
                if (ret == AVERROR_EOF) {
                    Util::logf("av_read_frame EOF.");
                    break;
                }
                else if (ret == AVERROR(EAGAIN)) {
                    Util::logf("av_read_frame EAGAIN.");
                    continue;
                }
                else if (ret < 0) {
                    Util::logf(ret, "av_read_frame fail.");
                    break;
                }

                if (param.OnReadPacket) {
                    ret = param.OnReadPacket(pFormatCtx, packet);
                    if (!ret) {
                        ok = false;
                        break;
                    }
                }
   
                // ����
                process_decode(param, packet);

                av_packet_unref(packet);
            } while (ok);

        } while (0);

        if (packet) {
            av_packet_free(&packet);
        }
        if (frmDecode) {
            av_frame_free(&frmDecode);
        }
        if (pktEncode) {
            av_packet_free(&pktEncode);
        }
        if (bufferConvert) {
            av_free(bufferConvert);
        }
        if (frmConvert) {
            av_frame_free(&frmConvert);
        }
        for (size_t i = 0; i < sizeof(swr_ctxs) / sizeof(swr_ctxs[0]); i++) {
            if (swr_ctxs[i]) {
                // sws_freeContext()
                swr_close(swr_ctxs[i]);
                swr_free(&swr_ctxs[i]);
            }
        }
        for (size_t i = 0; i < sizeof(sws_ctxs) / sizeof(sws_ctxs[0]); i++) {
            if (sws_ctxs[i]) {
                sws_freeContext(sws_ctxs[i]);
            }
        }
        for (size_t i = 0; i < sizeof(avctxDecodes) / sizeof(avctxDecodes[0]); i++)
        {
            if (avctxDecodes[i]) {
                avcodec_close(avctxDecodes[i]);
                avcodec_free_context(&avctxDecodes[i]);
            }
        }
        if (pFormatCtx) {
            avformat_close_input(&pFormatCtx);
        }

        return ret;
    }


    bool FFmpeg::init_decode(FFmpegOpenParam1& param)
    {
        if (!enableDecode) {
            return true;
        }


        int ret = -1;
        bool ok = true;

        // �򿪽�����
        for (const auto& it : param.decodeStreamIndexs)
        {
            auto streamIndex = it;
            if (streamIndex < 0|| streamIndex >= pFormatCtx->nb_streams) {
                Util::logf(ret, "streamIndex > %d", pFormatCtx->nb_streams);
                return false;
            }

            auto codecpar = pFormatCtx->streams[streamIndex]->codecpar;

            AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
            if (!codec) {
                Util::logf("avcodec_find_decoder fail %d.", codecpar->codec_id);
                ok = false;
                break;
            }

            AVCodecContext* avctx = avcodec_alloc_context3(codec);
            if (!avctx) {
                Util::logf("avcodec_alloc_context3 fail.");
                ok = false;
                break;
            }

            avcodec_parameters_to_context(avctx, codecpar);

            if (avcodec_open2(avctx, codec, NULL) < 0) {
                Util::logf("could not open codec");
                ok = false;
                break;
            }

            avctxDecodes[streamIndex] = avctx;
        }

        if (!ok) {
            return false;
        }

        frmDecode = av_frame_alloc();

        return true;
    }

    bool FFmpeg::init_sw(FFmpegOpenParam1& param)
    {
        if (!enableSWX) {
            return true;
        }

        int ret = -1;
        bool ok = true;

        frmConvert = av_frame_alloc();

        // ת��
        for (const auto& it : param.swxs)
        {
           auto streamIndex = it.first;
           auto output = it.second;
           if (streamIndex < 0 || streamIndex >= pFormatCtx->nb_streams) {
               Util::logf("streamIndex pFormatCtx->nb_streams, %d %d.", streamIndex, pFormatCtx->nb_streams);
               return false;
           }

           auto input = pFormatCtx->streams[streamIndex]->codecpar;

           if (input->codec_type == AVMEDIA_TYPE_AUDIO)
           {
               // ��Ƶ
               SwrContext* swr = swr_alloc_set_opts(nullptr,
                   output.channel_layout, (AVSampleFormat)output.format, output.sample_rate,
                   input->channel_layout, (AVSampleFormat)input->format, input->sample_rate,
                   0, NULL
               );

               if (!swr) {
                   Util::logf("swr_alloc_set_opts fail");
                   ok = false;
                   break;
               }

               ret = swr_init(swr);
               if (ret < 0) {
                   Util::logf("swr_init fail");
                   ok = false;
                   swr_close(swr);
                   swr_free(&swr);
                   break;
               }

               swr_ctxs[streamIndex] = swr;

               //int32_t size = av_samples_get_buffer_size(NULL, output.channels, output.frame_size, (AVSampleFormat)output.format, 1);
               //uint8_t* buffer = (uint8_t*)av_malloc(size);
           }
           else if (input->codec_type == AVMEDIA_TYPE_VIDEO)
           {
               // ��Ƶ
               SwsContext* sws = sws_getCachedContext(nullptr,
                   input->width, input->height, (AVPixelFormat)input->format,
                   output.width, output.height, (AVPixelFormat)output.format,
                   SWS_BILINEAR, NULL, NULL, NULL
               );
               if (!sws) {
                   Util::logf("sws_getCachedContext fail");
                   ok = false;
                   break;
               }

               sws_ctxs[streamIndex] = sws;

               int32_t size = av_image_get_buffer_size((AVPixelFormat)output.format, output.width, output.height, 1);
               bufferConvert = (uint8_t*)av_malloc(size);
               av_image_fill_arrays(frmConvert->data, frmConvert->linesize,
                   bufferConvert,
                   (AVPixelFormat)output.format, output.width, output.height, 1);
           }
        }

        if (!ok) {
            return false;
        }

        return true;
    }


    bool FFmpeg::init_encode(FFmpegOpenParam1& param)
    {
        if (!enableEncode) {
            return true;
        }

        int ret = -1;
        bool ok = true;


        // �򿪽�����
        for (const auto& it : param.encodes)
        {
            auto streamIndex = it.first;
            auto codecpar = it.second;
            if (streamIndex < 0 || streamIndex >= pFormatCtx->nb_streams) {
                Util::logf("streamIndex pFormatCtx->nb_streams, %d %d.", streamIndex, pFormatCtx->nb_streams);
                return false;
            }

            AVCodec* codec = avcodec_find_encoder(codecpar.codec_id);
            if (!codec) {
                Util::logf("avcodec_find_encoder fail %d.", codecpar.codec_id);
                ok = false;
                break;
            }

            AVCodecContext* avctx = avcodec_alloc_context3(codec);
            if (!avctx) {
                Util::logf("avcodec_alloc_context3 fail.");
                ok = false;
                break;
            }

            avcodec_parameters_to_context(avctx, &codecpar);

            switch (avctx->codec_type)
            {
            case AVMEDIA_TYPE_VIDEO:
                int FPS = 25;
                AVRational time_base{1, FPS };
                avctx->time_base = time_base;

                AVRational framerate{ FPS, 1};
                avctx->framerate = framerate; // ֡��25fps

                avctx->gop_size = FPS *10;     //  �ؼ�֡�����ÿn֡һ��I֡��
                avctx->max_b_frames = 0;       // ����B֡����߼����ԣ�

                // avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

                avctx->codec_tag = MKTAG('a', 'v', 'c', '1');

                // CRF ͨ����̬����ÿ֡������������QP�����ڱ������ۻ����ȶ���ǰ���·������ʡ����Ӷȸߵ�֡����������ʣ���֡��������ʣ�ʵ��������ѹ��Ч�ʵ�ƽ��
                // h264: 0~51, Ĭ��ֵ 23
                // h265: 0�C51, Ĭ��ֵ 28����ͬ�����±� H.264 ��ʡ 30%�C50% ���ʣ�
                // CRFֵ+-6�� ���ʼ���򷭱�
                // �Ӿ�����: 18 �ӽ�Դ���ʣ��ʺ�רҵ�浵
                // ��������ý��: 19-21 ϸ�ڱ������ã��ʺ�����/BվͶ��
                // ��׼������Ƶ: 22-25 ƽ�������������Ĭ��23��
                // �ƶ���/�ʹ���: 26-28 ���ģ���ɽ��ܣ���ʡ����
                // �����Ƶ/��ѹ��: 29+ ������ʧ���ԣ����޵���Ҫ������
                if (codec->id == AV_CODEC_ID_H264) {
                    // av_opt_set(avctx->priv_data, "preset", "slow", 0);

                    //av_dict_set(&param.optsEncode, "preset", "slow", 0); // ����������Ч
                    //av_dict_set(&param.optsEncode, "crf", "30", 0); // ����ѹ��������Ĭ��ֵ 23������Χ 0�C51��ֵԽС����Խ�ߣ��ļ�Խ��
                    //av_dict_set(&param.optsEncode, "tune", "film", 0); // ��������Ż�����film��Ӱ animation������grain��Ƭ������
                    //av_dict_set(&param.optsEncode, "x264-params", "no-scenecut=1:ref=3", 0); // ���ó����л����
                }

                break;
            }

            if (avcodec_open2(avctx, codec, &param.optsEncode) < 0) {
                Util::logf("could not open codec");
                ok = false;
                break;
            }

            avctxEncodes[streamIndex] = avctx;
        }

        if (!ok) {
            return false;
        }

        pktEncode = av_packet_alloc();

        return true;
    }

    bool FFmpeg::process_decode(FFmpegOpenParam1& param, AVPacket* packet)
    {
        if (!enableDecode) {
            return true;
        }

        int ret = -1;
        bool ok = true;
        auto it = avctxDecodes.find(packet->stream_index);
        if (it == avctxDecodes.end()) {
            return true;
        }

        AVCodecContext* avctx = it->second;

        ret = avcodec_send_packet(avctx, packet);
        if (ret < 0) {
            Util::logf(ret, "avcodec_send_packet fail");
            return false;
        }

        do
        {
            ret = avcodec_receive_frame(avctx, frmDecode);
            if (ret == AVERROR_EOF) {
                Util::logf("process_decode avcodec_receive_frame EOF");
                break;
            }
            else if (ret == AVERROR(EAGAIN)) {
                // Util::logf("process_decode avcodec_receive_frame EAGAIN");
                break;
            }
            else if (ret < 0) {
                Util::logf(ret, "process_decode avcodec_receive_frame fail");
                break;
            }

            // �ص�
            if (param.OnDecode) {
                ret = param.OnDecode(avctx, packet->stream_index, frmDecode);
                if (!ret) {
                    ok = false;
                    break;
                }
            }

            // ת��
            if (!process_sw(param, packet, frmDecode)) {
                ok = false;
                break;
            }

            if (enableSWX) {
                frmEncode = frmConvert;
            }
            else {
                frmEncode = frmDecode;
            }

            // ����
            if (!process_encode(param, packet, frmEncode)) {
                ok = false;
                break;
            }

            av_frame_unref(frmDecode);
        } while (ret >= 0 && ok);

        return ok;
    }

    bool FFmpeg::process_sw(FFmpegOpenParam1& param, AVPacket* packet, AVFrame* frame)
    {
        if (!enableSWX) {
            return true;
        }


        int ret = -1;
        bool ok = true;
        int32_t streamIndex = packet->stream_index;
        AVCodecParameters* codecpar = pFormatCtx->streams[streamIndex]->codecpar;

        if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            auto it = swr_ctxs.find(streamIndex);
            if (it == swr_ctxs.end()) {
                return true;
            }

            auto sw = it->second;
            ret = swr_convert_frame(sw, frame, frmConvert);
            if (ret < 0) {
                Util::logf(ret, "swr_convert_frame fail");
                return false;
            }
        }
        else if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            auto it = sws_ctxs.find(streamIndex);
            if (it == sws_ctxs.end()) {
                return true;
            }

            auto sw = it->second;
            ret = sws_scale(sw,
                (const uint8_t**)frame->data, frame->linesize, /* in */
                0, frame->height,
                frmConvert->data, frmConvert->linesize /* out */
            );
            if (ret < 0) {
                Util::logf(ret, "sws_scale fail");
                return false;
            }
        }

        if (param.OnConvert) {
            ret = param.OnConvert(codecpar, streamIndex, frmConvert);
            if (!ret) {
                return false;
            }
        }


        return true;
    }

    bool FFmpeg::process_encode(FFmpegOpenParam1& param, AVPacket* packet, AVFrame* frame)
    {
        if (!enableEncode) {
            return true;
        }


        int32_t ret = -1;
        bool ok = true;
        int32_t streamIndex = packet->stream_index;
        auto it = avctxEncodes.find(streamIndex);
        if (it == avctxEncodes.end()) {
            return true;
        }


        auto avctx = it->second;

        frame->pts = packet->pts;
        frame->format = avctx->pix_fmt;
        frame->width = avctx->width;
        frame->height = avctx->height;

        frame->color_range = avctx->color_range;
        frame->color_primaries = avctx->color_primaries;
        frame->color_trc = avctx->color_trc;
        frame->colorspace = avctx->colorspace;
        frame->chroma_location = avctx->chroma_sample_location;

        ret = avcodec_send_frame(avctx, frame);
        if (ret < 0) {
            Util::logf(ret, "avcodec_send_packet fail");
            return false;
        }


        do
        {
            ret = avcodec_receive_packet(avctx, pktEncode);
            if (ret == AVERROR_EOF) {
                Util::logf("process_encode avcodec_receive_packet EOF");
                break;
            }
            else if (ret == AVERROR(EAGAIN)) {
                // Util::logf("process_encode avcodec_receive_packet EAGAIN");
                break;
            }
            else if (ret < 0) {
                Util::logf(ret, "process_encode avcodec_receive_packet fail");
                ok = false;
                break;
            }


            if (param.OnEncode) {
                ret = param.OnEncode(avctx, pktEncode);
                if (!ret) {
                    ok = false;
                    break;
                }
            }

            av_packet_unref(pktEncode);
        } while (ret >= 0 && ok);

        return ok;
    }

    int32_t FFmpeg::Do(FFmpegOpenParam2& param)
    {
        const int32_t INBUF_SIZE = 20480;
        const int32_t REFILL_THRESH = 4096;

        int ret = -1;
        FILE* f = nullptr;
        AVCodecParserContext* parser = nullptr;
        AVCodec* codec = nullptr;
        AVCodecContext* avctx = nullptr;
        AVPacket* pkt = nullptr;
        uint8_t* inbuf = nullptr;
        uint8_t* data = nullptr;
        size_t   data_size = 0;
        bool ok = true;

        do
        {
            f = fopen(param.filename.data(), "rb");
            if (!f) {
                Util::logf("fopen fail %s.", param.filename.data());
                break;
            }

            inbuf = (uint8_t*)malloc(INBUF_SIZE);
            if (!inbuf) {
                Util::logf("malloc fail");
                break;
            }

            codec = avcodec_find_decoder(param.id);
            if (!codec) {
                Util::logf("avcodec_find_decoder fail %d.", param.id);
                break;
            }

            parser = av_parser_init(codec->id);
            if (!parser) {
                Util::logf("av_parser_init fail.");
                break;
            }

            avctx = avcodec_alloc_context3(codec);
            if (!avctx) {
                Util::logf("avcodec_alloc_context3 fail.");
                break;
            }

            ret = avcodec_open2(avctx, codec, NULL);
            if (ret < 0) {
                Util::logf(ret, "could not open codec");
                break;
            }

            if (param.cbOpen) {
                ret = param.cbOpen(avctx);
                if (!ret) { break; }
            }

            pkt = av_packet_alloc();
            if (!pkt) {
                Util::logf("av_packet_alloc fail");
                break;
            }

            data = inbuf;
            data_size = fread(inbuf, 1, INBUF_SIZE, f);

            while (data_size > 0)
            {
                ret = av_parser_parse2(parser, avctx, &pkt->data, &pkt->size,
                    data, data_size,
                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
                if (ret < 0) {
                    Util::logf(ret, "Error while parsing");
                    break;
                }

                data += ret;
                data_size -= ret;

                if (pkt->size) {
                    if (param.cbOnReadPacket) {
                        ret = param.cbOnReadPacket(avctx, pkt);
                        if (!ret) { break; }
                    }



                }

                if (data_size < REFILL_THRESH) {
                    memmove(inbuf, data, data_size);
                    data = inbuf;
                    size_t len = fread(data + data_size, 1, INBUF_SIZE - data_size, f);
                    if (len > 0) {
                        data_size += len;
                    }
                }
            }

        } while (0);

        if (pkt) {
            av_packet_free(&pkt);
        }
        if (parser) {
            av_parser_close(parser);
        }
        if (avctx) {
            avcodec_close(avctx);
            avcodec_free_context(&avctx);
        }
        if (inbuf) {
            free(inbuf);
        }
        if (f) {
            fclose(f);
        }

        return ret;
    }


    std::string Util::getResultErrorString(int errnum)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, errnum);

        return std::string(errbuf);
    }

    void Util::logf(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);

        va_list args_copy;
        va_copy(args_copy, args);

        //����ɱ������ӡ��ĳ���
        int len = vsnprintf(NULL, 0, fmt, args_copy);
        if (len > 0)
        {
            char* buffer = (char*)malloc(len + 1);
            if (buffer) {
                buffer[len] = '\0';
                vsnprintf(buffer, len + 1, fmt, args);
                printf("%s\n", buffer);
                free(buffer);
            }
        }

        va_end(args);

    }

    void Util::logf(int errnum, const char* fmt, ...)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, errnum);

        va_list args;
        va_start(args, fmt);

        va_list args_copy;
        va_copy(args_copy, args);

        //����ɱ������ӡ��ĳ���
        int len = vsnprintf(NULL, 0, fmt, args_copy);
        if (len > 0)
        {
            char* buffer = (char*)malloc(len + 1);
            if (buffer) {
                buffer[len] = '\0';
                vsnprintf(buffer, len + 1, fmt, args);
                printf("[errnum: %d, %s]; %s\n", errnum, errbuf, buffer);
                free(buffer);
            }
        }

        va_end(args);
    }


    void FFDecoder::Close()
    { 
        if (avctx) {
            avcodec_free_context(&avctx);
            avcodec_close(avctx);
            avctx = nullptr;
        }
    }

    int32_t FFDecoder::Init(AVCodecParameters* codecpar)
    {
        AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            Util::logf("avcodec_find_decoder fail %d.", codecpar->codec_id);
            return -1;
        }

        do
        {
            avctx = avcodec_alloc_context3(codec);
            if (!avctx) {
                Util::logf("avcodec_alloc_context3 fail.");
                break;
            }

            avcodec_parameters_to_context(avctx, codecpar);

            if (avcodec_open2(avctx, codec, NULL) < 0) {
                Util::logf("could not open codec");
                break;
            }

            memcpy(&this->codecpar, codecpar, sizeof(AVCodecParameters));

            return 0;
        } while (0);

        this->Close();

        return -1;
    }

    int32_t FFDecoder::SendPacket(AVPacket* pkt)
    {
        int ret = -1;
        if (!avctx) {
            Util::logf("SendPacket not init");
            return -1;
        }

        ret = avcodec_send_packet(avctx, pkt);
        if (ret < 0) {
            Util::logf(ret, "avcodec_send_packet fail");
        }

        return ret;
    }

    int32_t FFDecoder::ReceiveFrame(AVFrame* frame)
    {
        int ret = -1;
        if (!avctx) {
            Util::logf("ReceiveFrame not init");
            return -1;
        }

        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR_EOF) {
            Util::logf("avcodec_receive_frame EOF");
            return ret;
        }
        else if (ret == AVERROR(EAGAIN)) {
            // Util::logf("avcodec_receive_frame EAGAIN");
            return ret;
        }
        else if (ret < 0) {
            Util::logf(ret, "avcodec_receive_frame fail");
            return ret;
        }

        return ret;
    }

    void FFConvert::Close()
    {
        if (swr_ctx) {
            swr_free(&swr_ctx);
            swr_ctx = nullptr;
        }

        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }
    }

    int32_t FFConvert::Init(AVCodecParameters in, AVCodecParameters out)
    {
        this->input = in;
        this->output = out;
        if (input.codec_type == AVMEDIA_TYPE_AUDIO && output.codec_type == AVMEDIA_TYPE_AUDIO)
        {
            swr_ctx = swr_alloc_set_opts(swr_ctx,
                output.channel_layout, (AVSampleFormat)output.format, output.sample_rate,
                input.channel_layout, (AVSampleFormat)input.format, input.sample_rate,
                0, NULL
            );
            if (!swr_ctx) {
                Util::logf("swr_alloc_set_opts fail");
                return -1;
            }

            int ret = swr_init(swr_ctx);
            if (ret < 0) {
                Util::logf(ret, "swr_init fail");
                this->Close();
                return -1;
            }

            return 0;
        }
        else if (input.codec_type == AVMEDIA_TYPE_VIDEO && output.codec_type == AVMEDIA_TYPE_VIDEO)
        {
            sws_ctx = sws_getCachedContext(sws_ctx,
                input.width, input.height, (AVPixelFormat)input.format,
                output.width, output.height, (AVPixelFormat)output.format,
                SWS_BILINEAR, NULL, NULL, NULL
            );
            if (!sws_ctx) {
                Util::logf("sws_getCachedContext fail");
                return -1;
            }

            return 0;
        }
        else {
            Util::logf("MiddleSWX::Init param error");
            return -1;
        }
    }

    int32_t FFConvert::Do(AVFrame* in, AVFrame* out)
    {
        int ret = -1;
        if (swr_ctx && input.codec_type == AVMEDIA_TYPE_AUDIO && output.codec_type == AVMEDIA_TYPE_AUDIO)
        {
            ret = swr_convert_frame(swr_ctx, out, in);
            if (ret < 0) {
                Util::logf(ret, "swr_convert_frame fail");;
                return ret;
            }
        }
        else if (sws_ctx && input.codec_type == AVMEDIA_TYPE_VIDEO && output.codec_type == AVMEDIA_TYPE_VIDEO)
        {
            ret = sws_scale(sws_ctx,
                (const uint8_t**)in->data, in->linesize, /* in */
                0, in->height,
                out->data, out->linesize /* out */
            );
            if (ret < 0) {
                Util::logf(ret, "sws_scale fail");;
                return ret;
            }
        }

        return 0;
    }

    void FFEncoder::Close()
    {
        if (avctx) {
            avcodec_free_context(&avctx);
            avcodec_close(avctx);
            avctx = nullptr;
        }
    }


    int32_t FFEncoder::Init(AVCodecParameters* codecpar)
    {
        AVCodec* codec = avcodec_find_encoder(codecpar->codec_id);
        if (!codec) {
            Util::logf("avcodec_find_encoder fail %d.", codecpar->codec_id);
            return -1;
        }


        do
        {
            avctx = avcodec_alloc_context3(codec);
            if (!avctx) {
                Util::logf("avcodec_alloc_context3 fail.");
                break;
            }

            avcodec_parameters_to_context(avctx, codecpar);

            switch (avctx->codec_type)
            {
            case AVMEDIA_TYPE_AUDIO:
                break;
            case AVMEDIA_TYPE_VIDEO:

                int FPS = 25;
                AVRational time_base{ 1, FPS };
                avctx->time_base = time_base;

                AVRational framerate{ FPS, 1 };
                avctx->framerate = framerate; // ֡��25fps

                avctx->gop_size = FPS * 10;     //  �ؼ�֡�����ÿn֡һ��I֡��
                avctx->max_b_frames = 0;       // ����B֡����߼����ԣ�

                // avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

                if (codec->id == AV_CODEC_ID_H264) {
                    avctx->codec_tag = MKTAG('a', 'v', 'c', '1');
                    av_opt_set(avctx->priv_data, "preset", "slow", 0);
                }
                break;
            }

            int ret = avcodec_open2(avctx, codec, NULL);
            if (ret < 0) {
                Util::logf(ret, "could not open codec");
                break;
            }

            memcpy(&this->codecpar, codecpar, sizeof(AVCodecParameters));

            return 0;
        } while (0);

        this->Close();

        return -1;
    }

    int32_t FFEncoder::SendFrame(AVFrame* frame)
    {
        int32_t ret = -1;
        if (!avctx) {
            Util::logf("SendFrame not init");
            return -1;
        }

        ret = avcodec_send_frame(avctx, frame);
        if (ret < 0) {
            Util::logf(ret, "avcodec_send_frame fail");
        }

        return ret;
    }

    int32_t FFEncoder::ReceivePacket(AVPacket* pkt)
    {
        int ret = -1;
        if (!avctx) {
            Util::logf("ReceivePacket not init");
            return -1;
        }

        ret = avcodec_receive_packet(avctx, pkt);
        if (ret == AVERROR_EOF) {
            Util::logf("avcodec_receive_packet EOF");
            return ret;
        }
        else if (ret == AVERROR(EAGAIN)) {
            // Util::logf("avcodec_receive_packet EAGAIN");
            return ret;
        }
        else if (ret < 0) {
            Util::logf(ret, "avcodec_receive_packet fail");
            return ret;
        }

        return ret;
    }

    void FFEncoder::add_adts_header(uint8_t* header, int data_len, int profile, int sample_rate, int channels)
    {
        int sample_rate_idx = 3; // Ĭ��48000Hz������3��
        const int freq_table[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
        for (int i = 0; i < 13; i++) {
            if (freq_table[i] == sample_rate) {
                sample_rate_idx = i;
                break;
            }
        }

        int frame_len = data_len + 7; // ADTSͷ7�ֽ� + AAC���ݳ���

        // ����ADTSͷ��7�ֽڣ�
        header[0] = 0xFF; // Syncword��8λ
        header[1] = 0xF0; // Syncword��4λ + ������־
        header[1] |= (0 << 3); // MPEG-4��ʶ
        header[1] |= 1; // ��CRCУ��
        header[2] = (profile << 6) | (sample_rate_idx << 2) | ((channels & 0x4) >> 2);
        header[3] = ((channels & 0x3) << 6) | (frame_len >> 11);
        header[4] = (frame_len >> 3) & 0xFF;
        header[5] = ((frame_len & 0x7) << 5) | 0x1F;
        header[6] = 0xFC;
    }
}
