#define _CRT_SECURE_NO_WARNINGS
#include "ffmpeg4.h"

#ifdef _MSC_VER
#pragma comment(lib, "../ffmpeg/lib/avformat.lib")
#pragma comment(lib, "../ffmpeg/lib/avcodec.lib")
#pragma comment(lib, "../ffmpeg/lib/avfilter.lib")
#pragma comment(lib, "../ffmpeg/lib/avutil.lib")
#pragma comment(lib, "../ffmpeg/lib/swresample.lib")
#pragma comment(lib, "../ffmpeg/lib/swscale.lib")
#endif // _MSC_VER

int main()
{
	const char* filename = "20250804_092926.mp4";

	ffmpeg4::FFDecoder decoder;
	ffmpeg4::FFEncoder encoder;

	ffmpeg4::FFmpegOpenParam1 p;

	p.filename = filename;
	p.OnOpen = [&](AVFormatContext* fmtCtx) -> bool {
		for (uint32_t i = 0; i < fmtCtx->nb_streams; i++)
		{
			auto codecpar = fmtCtx->streams[i]->codecpar;
			printf("%d\n", codecpar->codec_id);

			if (codecpar->codec_id == AV_CODEC_ID_AAC) {
				decoder.Init(codecpar);
				// codecpar->bit_rate = 128 * 1024;
				encoder.Init(codecpar);
			}

			if (codecpar->codec_id == AV_CODEC_ID_AAC) {
				// 解码
				p.decodeStreamIndexs.push_back(i);
			}


			if (0 && codecpar->codec_id == AV_CODEC_ID_H264) {

				// 解码
				p.decodeStreamIndexs.push_back(i);

				// 缩放
				AVCodecParameters codecpar1;
				memset(&codecpar1, 0, sizeof(AVCodecParameters));
				codecpar1.width = 640;
				codecpar1.height = 480;
				codecpar1.format = AV_PIX_FMT_YUV420P;

				p.swxs.insert(std::make_pair(i, codecpar1));

				AVCodecParameters codecpar2;
				memset(&codecpar2, 0, sizeof(AVCodecParameters));
				codecpar2.codec_id = AV_CODEC_ID_H264;
				codecpar2.bit_rate = 750823; //2*1024*1024; // 1 997 769
				codecpar2.width = codecpar1.width;
				codecpar2.height = codecpar1.height;
				codecpar2.format = codecpar1.format;
				codecpar2.profile = FF_PROFILE_H264_HIGH;
				codecpar2.level = 30; // 41

				codecpar2.field_order = AV_FIELD_UNKNOWN;

				codecpar2.color_range = AVCOL_RANGE_MPEG;
				codecpar2.color_primaries = AVCOL_PRI_UNSPECIFIED;
				codecpar2.color_trc = AVCOL_TRC_UNSPECIFIED;
				codecpar2.color_space = AVCOL_SPC_UNSPECIFIED;
				codecpar2.chroma_location = AVCHROMA_LOC_LEFT;

				av_dict_set(&p.optsEncode, "preset", "slow", 0); // 更慢但更高效
				av_dict_set(&p.optsEncode, "crf", "30", 0); // 控制压缩质量（默认值 23），范围 0–51，值越小画质越高，文件越大
				//av_dict_set(&p.optsEncode, "tune", "film", 0); // 针对视频内容优化
				//av_dict_set(&p.optsEncode, "x264-params", "no-scenecut=1:ref=3", 0); // 禁用场景切换检测
				av_dict_set(&p.optsEncode, "framerate", "25", 0);

				// 编码
				p.encodes.insert(std::make_pair(i, codecpar2));
			}
		}

		return true;
	};

	AVFrame* frame = av_frame_alloc();
	p.OnReadPacket = [&](AVFormatContext* fmtCtx, AVPacket* packet)->bool {
		printf(">>>> OnReadPacket: %d %I64d\n", packet->stream_index, packet->pts);


		// 测试解码
		if (decoder.GetCodecpar()->codec_id == fmtCtx->streams[packet->stream_index]->codecpar->codec_id) {
			int ret = decoder.SendPacket(packet);
			while (!ret)
			{
				ret = decoder.ReceiveFrame(frame);
				if (!ret) {
					// 处理数据
					av_frame_unref(frame);
				}
				else {
					break;
				}
			}
		}

		return true;
	};

	AVPacket* packet = av_packet_alloc();
	FILE* fpAAC = fopen("20250804_092926_re.aac", "wb+");
	p.OnDecode = [&](AVCodecContext* codecCtx, int32_t streamIndex, AVFrame* frame)->bool {
		if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
			int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)frame->format);
			int valid_size = frame->channels * frame->nb_samples * bytes_per_sample;

			frame->linesize[0] = valid_size;

			int ret = encoder.SendFrame(frame);
			while (!ret)
			{
				ret = encoder.ReceivePacket(packet);
				if (!ret) {
					auto profile = codecCtx->profile;
					auto channels = codecCtx->channels;
					auto sample_rate = codecCtx->sample_rate;
					codecCtx->channel_layout;
					codecCtx->bit_rate;

					uint8_t adts_header[7];
					encoder.add_adts_header(adts_header, packet->size, profile, sample_rate, channels);
					fwrite(adts_header, 1, 7, fpAAC);
					fwrite(packet->data, 1, packet->size, fpAAC);

					av_packet_unref(packet);
				}
				else {
					break;
				}
			}
		}

		return true;
	};
	p.OnConvert = [&](AVCodecParameters* codecpar, int32_t streamIndex, AVFrame* frame)->bool {
		return true;
	};

	FILE* fp2 = fopen("out.h264", "wb+");
	p.OnEncode = [&](AVCodecContext* codecCtx, AVPacket* packet)->bool {
		printf("pts: %I64d\n", packet->pts);
		fwrite(packet->data, 1, packet->size, fp2);
		return true;
	};

	ffmpeg4::FFmpeg ff;
	ff.Do(p);

	fclose(fp2);
	fclose(fpAAC);
	av_frame_free(&frame);
	av_packet_free(&packet);
}