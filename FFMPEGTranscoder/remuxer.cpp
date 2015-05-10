/*
* Copyright (c) 2013 Stefano Sabatini
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

/**
* @file
* libavformat/libavcodec demuxing and muxing API example.
*
* Remux streams from one container format to another.
* @example remuxing.c
*/

#include "makefile.h"


#if _MSC_VER
#define snprintf _snprintf
#endif
#define __STDC_CONSTANT_MACROS 

#define MAX_AUDIO_FRAME_SIZE 192000
#define assert(cond) if((cond)) exit(1)

extern "C" {
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
}
#define NUM_OF_STREAMS 2


static AVFormatContext* _inputFormatContexts[NUM_OF_STREAMS];
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

#if 0
    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
        tag,
        av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
        av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
        av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
        pkt->stream_index);
#endif // 0

}

/* check that a given sample format is supported by the encoder */
static int check_sample_fmt(AVCodec *codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat *p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

static int open_input_file2(const char *filename, AVFormatContext** input_format_ctx)
{
    int ret;
    unsigned int i;

    *input_format_ctx = NULL;
    if ((ret = avformat_open_input(&(*input_format_ctx), filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(*input_format_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    for (i = 0; i < (*input_format_ctx)->nb_streams; i++) {
        AVStream *stream;
        AVCodecContext *codecCtx;
        stream = (*input_format_ctx)->streams[i];
        codecCtx = stream->codec;
        /* Reencode video & audio and remux subtitles etc. */
        if (codecCtx->codec_type == AVMEDIA_TYPE_VIDEO
            || codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* Open decoder */
            ret = avcodec_open2(codecCtx,
                avcodec_find_decoder(codecCtx->codec_id), NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }
    }

    av_dump_format(*input_format_ctx, 0, filename, 0);
    return 0;
}



#ifdef REMUXING
int main(int argc, char **argv)
#else
int main_dummy(int argc, char **argv)
#endif
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *inVideoFmtCtx = NULL, *inAudioFmtCtx = NULL, *outFmtCtx = NULL;
    AVPacket pkt;
    const char *inVideo_filename, *inAudio_filename, *out_filename;
    int ret, i;

    if (argc < 3) {
        printf("usage: %s input output\n"
            "API example program to remux a media file with libavformat and libavcodec.\n"
            "The output format is guessed according to the file extension.\n"
            "\n", argv[0]);
        return 1;
    }

    inVideo_filename = argv[1];
    inAudio_filename = argv[2];
    out_filename = argv[3];

    av_register_all();
    /* =============== VIDEO STREAM ================*/

    //if ((ret = open_input_file2(argv[1])) < 0)
    //    goto end;


    if ((ret = avformat_open_input(&inVideoFmtCtx, inVideo_filename, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", inVideo_filename);
        goto end;
    }

    if ((ret = avformat_find_stream_info(inVideoFmtCtx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }

    av_dump_format(inVideoFmtCtx, 0, inVideo_filename, 0);



    /* =============== AUDIO STREAM ================*/

    //if ((ret = open_output_file2(argv[2])) < 0)
    //    goto end;
    if ((ret = avformat_open_input(&inAudioFmtCtx, inAudio_filename, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", inAudio_filename);
        goto end;
    }

    if ((ret = avformat_find_stream_info(inAudioFmtCtx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }

    av_dump_format(inAudioFmtCtx, 0, inAudio_filename, 0);


    avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, out_filename);
    if (!outFmtCtx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ofmt = outFmtCtx->oformat;


    /* =============== VIDEO STREAM ================*/
    for (i = 0; i < inVideoFmtCtx->nb_streams; i++) {
        AVStream *in_stream = inVideoFmtCtx->streams[i];
        AVStream *out_stream = avformat_new_stream(outFmtCtx, in_stream->codec->codec);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
            goto end;
        }
        out_stream->codec->codec_tag = 0;
        if (outFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    /* =============== AUDIO STREAM ================*/
    for (i = 0; i < inAudioFmtCtx->nb_streams; i++) {
        AVStream *in_stream = inAudioFmtCtx->streams[i];
#if 1
        AVStream *out_stream = avformat_new_stream(outFmtCtx, in_stream->codec->codec);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }


        // Duplicate codec from input stream
        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
            goto end;
        }
#else

        // Allocate MP3 codec
        AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
        if (!codec) {
            fprintf(stderr, "Codec not found\n");
            exit(1);
        }

        AVStream *out_stream = avformat_new_stream(outFmtCtx, codec);
        AVCodecContext *codecCtx = out_stream->codec;
        /* put sample parameters */
        codecCtx->bit_rate = 64000;

        /* check that the encoder supports s16 pcm input */
        codecCtx->sample_fmt = AV_SAMPLE_FMT_S16P;
        if (!check_sample_fmt(codec, codecCtx->sample_fmt)) {
            fprintf(stderr, "Encoder does not support sample format %s",
                av_get_sample_fmt_name(codecCtx->sample_fmt));
            exit(1);
        }

        /* select other audio parameters supported by the encoder */
        codecCtx->sample_rate = 44100;
        codecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
        codecCtx->channels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);

        /* open it */
        if (avcodec_open2(codecCtx, codec, NULL) < 0) {
            fprintf(stderr, "Could not open codec\n");
            exit(1);
        }

#endif // COPY/ALLOCATE codec context


        out_stream->codec->codec_tag = 0;
        if (outFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    av_dump_format(outFmtCtx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outFmtCtx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    ret = avformat_write_header(outFmtCtx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    while (1) {
        AVStream *in_stream, *out_stream;

        /* =============== VIDEO STREAM ================*/
        ret = av_read_frame(inVideoFmtCtx, &pkt);
        if (ret < 0)
            break;

        in_stream = inVideoFmtCtx->streams[pkt.stream_index];
        out_stream = outFmtCtx->streams[pkt.stream_index];

        log_packet(inVideoFmtCtx, &pkt, "in");

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(outFmtCtx, &pkt, "out");

        ret = av_interleaved_write_frame(outFmtCtx, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_free_packet(&pkt);


        /* =============== AUDIO STREAM ================*/

        ret = av_read_frame(inAudioFmtCtx, &pkt);
        if (ret < 0)
            break;
        in_stream = inAudioFmtCtx->streams[pkt.stream_index];

#if 0
        AVCodec *aCodec = avcodec_find_decoder(in_stream->codec->codec_id);
        if (!aCodec) {
            fprintf(stderr, "Unsupported codec!\n");
            return -1;
        }
        // Copy context
        AVCodecContext *inAudioCodecCtx = avcodec_alloc_context3(aCodec);
        if (avcodec_copy_context(inAudioCodecCtx, in_stream->codec) != 0) {
            fprintf(stderr, "Couldn't copy codec context");
            return -1; // Error copying codec context
        }
        avcodec_open2(inAudioCodecCtx, aCodec, NULL);

        /* frame containing input raw audio */
        AVFrame *decodedFrame = av_frame_alloc();
        if (!decodedFrame) {
            fprintf(stderr, "Could not allocate audio frame\n");
            exit(1);
        }
        int gotFrame;
        int bytesDecoded = avcodec_decode_audio4(inAudioCodecCtx, decodedFrame, &gotFrame, &pkt);
#endif // 0


        pkt.stream_index++;
        out_stream = outFmtCtx->streams[pkt.stream_index];

        log_packet(inAudioFmtCtx, &pkt, "in");

#if 0
        /* frame containing input raw audio */
        AVFrame *frame2Encode = av_frame_alloc();
        if (!frame2Encode) {
            fprintf(stderr, "Could not allocate audio frame\n");
            exit(1);
        }
        frame2Encode->nb_samples = out_stream->codec->frame_size;
        frame2Encode->format = out_stream->codec->sample_fmt;
        frame2Encode->channel_layout = out_stream->codec->channel_layout;
#endif // 0


#if 0
        /* the codec gives us the frame size, in samples,
        * we calculate the size of the samples buffer in bytes */
        int buffer_size = av_samples_get_buffer_size(NULL, out_stream->codec->channels, out_stream->codec->frame_size,
            out_stream->codec->sample_fmt, 0);
        if (buffer_size < 0) {
            fprintf(stderr, "Could not get sample buffer size\n");
            exit(1);
        }
        uint16_t *samples = (uint16_t*)(av_malloc(buffer_size));
        if (!samples) {
            fprintf(stderr, "Could not allocate %d bytes for samples buffer\n",
                buffer_size);
            exit(1);
        }
        /* setup the data pointers in the AVFrame */
        ret = avcodec_fill_audio_frame(frame2Encode, out_stream->codec->channels, out_stream->codec->sample_fmt,
            (const uint8_t*)samples, buffer_size, 0);
        if (ret < 0) {
            fprintf(stderr, "Could not setup audio frame\n");
            exit(1);
        }
#endif // 0

#if 0
        AVPacket encodedPacket;
        int gotPacket;
        av_init_packet(&encodedPacket);
        encodedPacket.data = NULL; // packet data will be allocated by the encoder
        encodedPacket.size = 0;
        for (;;)
        {
            int success = avcodec_encode_audio2(out_stream->codec, &encodedPacket, decodedFrame, &gotPacket);
            ret = av_read_frame(inAudioFmtCtx, &pkt);
            if (ret < 0 || gotPacket)
            {
                break;
            }
            else
            {
                bytesDecoded = avcodec_decode_audio4(inAudioCodecCtx, decodedFrame, &gotFrame, &pkt);
            }
        }
#endif // 0

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(outFmtCtx, &pkt, "out");

        ret = av_interleaved_write_frame(outFmtCtx, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_free_packet(&pkt);
    }

    av_write_trailer(outFmtCtx);
end:

    avformat_close_input(&inVideoFmtCtx);
    avformat_close_input(&inAudioFmtCtx);

    /* close output */
    if (outFmtCtx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&outFmtCtx->pb);
    avformat_free_context(outFmtCtx);

    if (ret < 0 && ret != AVERROR_EOF) {
        //fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}