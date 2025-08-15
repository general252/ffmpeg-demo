#pragma once

#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <functional>
#include <vector>
#include <map>

#ifdef __cplusplus
extern "C" {
#endif
    #include "libavcodec/avcodec.h"
    #include "libavutil/opt.h"
    #include "libavutil/imgutils.h"
    #include "libswscale/swscale.h"
    #include "libavformat/avformat.h"
    #include "libavutil/timestamp.h"
    #include "libavfilter/avfilter.h"
    #include "libavfilter/buffersink.h"
    #include "libavfilter/buffersrc.h"
    #include "libswresample/swresample.h"
    #include "libavutil/audio_fifo.h"
#ifdef __cplusplus
}
#endif

namespace ffmpeg4
{
    class Util {
    public:
        static std::string getResultErrorString(int errnum);
        static void logf(const char* fmt, ...);
        static void logf(int errnum, const char* fmt, ...);
    };

    // ����
    class FFDecoder {
    public:
        void Close();
        int32_t Init(AVCodecParameters* codecpar);
        int32_t SendPacket(AVPacket* pkt);
        // AVFrame* frame = av_frame_alloc(); // av_frame_free(&frame);
        // ReceiveFrame()
        // av_frame_unref(frame)
        int32_t ReceiveFrame(AVFrame *frame);

    public:
        const AVCodecContext* GetContext() { return avctx; }
        const AVCodecParameters* GetCodecpar() { return &codecpar; }

    private:
        AVCodecContext* avctx = nullptr;
        AVCodecParameters codecpar;
    };

    // audio resample, video scale
    // swr/sws
    class FFConvert {
    public:
        void Close();
        int32_t Init(AVCodecParameters in, AVCodecParameters out);
        int32_t Do(AVFrame* in, AVFrame* out);

    private:
        AVCodecParameters input;
        AVCodecParameters output;
        SwrContext* swr_ctx = nullptr;
        SwsContext* sws_ctx = nullptr;
    };

    // ����
    class FFEncoder {
    public:
        void Close();
        int32_t Init(AVCodecParameters* codecpar);
        int32_t SendFrame(AVFrame* frame);
        // AVPacket* packet = av_packet_alloc(); // av_packet_free(&packet);
        // ReceivePacket()
        // av_packet_unref(packet);
        int32_t ReceivePacket(AVPacket* pkt);

    public:
        const AVCodecContext* GetContext() { return avctx; }
        const AVCodecParameters* GetCodecpar() { return &codecpar; }

    public:
        // uint8_t adts_header[7];
        // add_adts_header(adts_header, pkt->size, profile, sample_rate, channels);
        // fwrite(adts_header, 1, 7, out_file);
        // fwrite(pkt->data, 1, pkt->size, out_file);
        void add_adts_header(uint8_t* header, int data_len, int profile, int sample_rate, int channels);

    private:
        AVCodecContext* avctx = nullptr;
        AVCodecParameters codecpar;
    };

    class FFmpegOpenParam1 {
    public:
        // �⸴��
        using CallbackOnOpen = std::function<bool(AVFormatContext*)>;
        using CallbackOnReadPacket = std::function<bool(AVFormatContext*, AVPacket*)>;

        std::string filename;
        AVInputFormat* fmt = nullptr;
        AVDictionary* option = nullptr;

        CallbackOnOpen OnOpen = nullptr;
        CallbackOnReadPacket OnReadPacket = nullptr;

    public:
        // ����
        using CallbackOnDecode = std::function<bool(AVCodecContext*, int32_t streamIndex, AVFrame*)>;

        std::vector<int32_t> decodeStreamIndexs; // ����� stream index
        // ��Ƶ��Ч��С
        // int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)frame->format);
        // int valid_size = frame->channels * frame->nb_samples * bytes_per_sample;
        CallbackOnDecode OnDecode = nullptr;

    public:
        // �ز���/����
        using CallbackOnSWX = std::function<bool(AVCodecParameters*, int32_t streamIndex, AVFrame*)>;

        std::map<int32_t, AVCodecParameters> swxs;// ��Ƶ�ز���, ��Ƶ����
        CallbackOnSWX OnConvert = nullptr;

    public:
        // ����
        using CallbackOnEncode = std::function<bool(AVCodecContext*, AVPacket*)>;
        AVDictionary* optsEncode = nullptr;
        std::map<int32_t, AVCodecParameters> encodes;
        CallbackOnEncode OnEncode = nullptr;
    };

    class FFmpegOpenParam2 {
    public:
        // ����
        using CallbackOnOpen = std::function<int32_t(AVCodecContext*)>;
        using CallbackOnReadPacket = std::function<int32_t(AVCodecContext*, AVPacket*)>;

        AVCodecID id = AV_CODEC_ID_NONE;
        std::string filename;

        CallbackOnOpen cbOpen = nullptr;
        CallbackOnReadPacket cbOnReadPacket = nullptr;
    };

    class FFmpeg
    {
    public:
        int32_t Do(FFmpegOpenParam1& param);

    protected:
        int32_t Do(FFmpegOpenParam2& param);

    protected:
        bool init_decode(FFmpegOpenParam1& param);
        bool init_sw(FFmpegOpenParam1& param);
        bool init_encode(FFmpegOpenParam1& param);

        bool process_decode(FFmpegOpenParam1& param, AVPacket* packet);
        // packet: ����ʶԴ
        bool process_sw(FFmpegOpenParam1& param, AVPacket* packet, AVFrame* frame);
        // packet: ����ʶԴ
        bool process_encode(FFmpegOpenParam1& param, AVPacket* packet, AVFrame* frame);

    private:
        // ����
        AVFormatContext* pFormatCtx = nullptr;
        AVPacket* packet = nullptr;

        // ����
        std::map<int32_t, AVCodecContext*> avctxDecodes;
        AVFrame* frmDecode = nullptr;
        bool enableDecode = false;

        // �ز���
        std::map<int32_t, SwrContext*> swr_ctxs;
        std::map<int32_t, SwsContext*> sws_ctxs;
        uint8_t* bufferConvert = nullptr;
        AVFrame* frmConvert = nullptr;
        bool enableSWX = false;

        // ����
        std::map<int32_t, AVCodecContext*> avctxEncodes;
        AVFrame* frmEncode = nullptr; // �����frame
        AVPacket* pktEncode = nullptr;
        bool enableEncode = false;
    };

}
