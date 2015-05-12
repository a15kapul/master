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
#include <libavfilter/avfilter.h>
#include <libavfilter/avfiltergraph.h>
#include <libavutil/opt.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}
#define NUM_OF_STREAMS 2

/*  ====== DATA TYPES DEFINITIONS ======  */
typedef struct FilteringContext {
    AVFilterContext* BuffersinkCtx;
    AVFilterContext* BuffersrcCtx;
    AVFilterGraph* FilterGraph;
} FilteringContext;


/*  ====== GLOBAL VARIABLES ======  */
static AVFormatContext* _inputFormatContexts[NUM_OF_STREAMS];
static AVFormatContext* _outputFormatContext;
static FilteringContext* _filterCtx;




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
        if (codecCtx->codec_type == AVMEDIA_TYPE_VIDEO || codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* Open decoder */
            ret = avcodec_open2(codecCtx, avcodec_find_decoder(codecCtx->codec_id), NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            }
        }
    }

    av_dump_format(*input_format_ctx, 0, filename, 0);
    return 0;
}

static int init_filter(FilteringContext* fctx, AVCodecContext *dec_ctx,
    AVCodecContext *enc_ctx, const char *filter_spec)
{
    char args[512];
    int ret = 0;
    AVFilter *bufferSrc = NULL;
    AVFilter *bufferSink = NULL;
    AVFilterContext* bufferSrcCtx = NULL;
    AVFilterContext* bufferSinkCtx = NULL;
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVFilterGraph* filterGraph = avfilter_graph_alloc();

    if (!outputs || !inputs || !filterGraph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        bufferSrc = avfilter_get_by_name("buffer");
        bufferSink = avfilter_get_by_name("buffersink");
        if (!bufferSrc || !bufferSink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            dec_ctx->time_base.num, dec_ctx->time_base.den,
            dec_ctx->sample_aspect_ratio.num,
            dec_ctx->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&bufferSrcCtx, bufferSrc, "in",
            args, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&bufferSinkCtx, bufferSink, "out",
            NULL, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(bufferSinkCtx, "pix_fmts",
            (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
            AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    }
    else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        bufferSrc = avfilter_get_by_name("abuffer");
        bufferSink = avfilter_get_by_name("abuffersink");
        if (!bufferSrc || !bufferSink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (!dec_ctx->channel_layout) {
            dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
        }
        snprintf(args, sizeof(args),
            "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
            dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
            av_get_sample_fmt_name(dec_ctx->sample_fmt),
            dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&bufferSrcCtx, bufferSrc, "in", args, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&bufferSinkCtx, bufferSink, "out", NULL, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(bufferSinkCtx, "sample_fmts", (uint8_t*)&enc_ctx->sample_fmt,
            sizeof(enc_ctx->sample_fmt), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        ret = av_opt_set_bin(bufferSinkCtx, "channel_layouts", (uint8_t*)&enc_ctx->channel_layout,
            sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(bufferSinkCtx, "sample_rates", (uint8_t*)&enc_ctx->sample_rate,
            sizeof(enc_ctx->sample_rate), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    }
    else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = bufferSrcCtx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = bufferSinkCtx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filterGraph, filter_spec,
        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filterGraph, NULL)) < 0)
        goto end;

    /* Fill FilteringContext */
    fctx->BuffersrcCtx = bufferSrcCtx;
    fctx->BuffersinkCtx = bufferSinkCtx;
    fctx->FilterGraph = filterGraph;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

// TODO: filters should be stored in matrix Mat[M][N], where M = index of output_context and N = index of input_stream
//       map to translate stream index in input contexts to stream index in output context
static int init_filters(AVFormatContext* input_context, AVFormatContext* output_context)
{
    const char* filterSpec;
    unsigned int i;
    int ret;
    _filterCtx = (FilteringContext*)av_malloc_array(input_context->nb_streams, sizeof(*_filterCtx));
    if (!_filterCtx)
        return AVERROR(ENOMEM);
    for (i = 0; i < input_context->nb_streams; i++) {
            _filterCtx[i].BuffersrcCtx = NULL;
            _filterCtx[i].BuffersinkCtx = NULL;
            _filterCtx[i].FilterGraph = NULL;
            if (!(input_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO
                || input_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO))
                continue;

            if (input_context->streams[i]->codec->codec_id == output_context->streams[i+1]->codec->codec_id)
                continue;

            if (input_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
                filterSpec = "null"; /* passthrough (dummy) filter for video */
            else
                filterSpec = "anull"; /* passthrough (dummy) filter for audio */
            ret = init_filter(&_filterCtx[i], input_context->streams[i]->codec, output_context->streams[i+1]->codec, filterSpec);
            if (ret)
                return ret;

    }
    return 0;
}

static int encode_write_frame(AVFrame *filt_frame, AVFormatContext* in_format_ctx, AVFormatContext* out_format_ctx, unsigned int stream_index, int *got_frame) {
    int ret;
    int gotFrameLocal;
    AVPacket encPkt;

    if (!got_frame)
        got_frame = &gotFrameLocal;

    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    /* encode filtered frame */
    encPkt.data = NULL;
    encPkt.size = 0;
    av_init_packet(&encPkt);
    ret = avcodec_encode_audio2(out_format_ctx->streams[stream_index + 1]->codec, &encPkt,
        filt_frame, got_frame);
    av_frame_free(&filt_frame);
    if (ret < 0)
        return ret;
    if (!(*got_frame))
        return 0;

    /* prepare packet for muxing */
    encPkt.stream_index = stream_index;
    av_packet_rescale_ts(&encPkt,
        out_format_ctx->streams[stream_index+1]->codec->time_base,
        out_format_ctx->streams[stream_index+1]->time_base);

    av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
    /* mux encoded frame */
    ret = av_interleaved_write_frame(out_format_ctx, &encPkt);
    return ret;
}

static int filter_encode_write_frame(AVFrame *frame, AVFormatContext* in_format_ctx, AVFormatContext* out_format_ctx, unsigned int stream_index)
{
    int ret;
    AVFrame *filtFrame;

    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(_filterCtx[stream_index].BuffersrcCtx,
        frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        filtFrame = av_frame_alloc();
        if (!filtFrame) {
            ret = AVERROR(ENOMEM);
            break;
        }
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(_filterCtx[stream_index].BuffersinkCtx,
            filtFrame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
            * if flushed and no more frames for output - returns AVERROR_EOF
            * rewrite retcode to 0 to show it as normal procedure completion
            */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            av_frame_free(&filtFrame);
            break;
        }

        filtFrame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(filtFrame, in_format_ctx, out_format_ctx, stream_index, NULL);
        if (ret < 0)
            break;
    }

    return ret;
}

static int setup_mp3_audio_codec(AVFormatContext* out_fmt_ctx)
{
    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    AVStream *out_stream = avformat_new_stream(out_fmt_ctx, codec);
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

    out_stream->codec->codec_tag = 0;
    if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

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

    /* =============== OPEN STREAMS ================*/

    if ((ret = open_input_file2(argv[1], &inVideoFmtCtx)) < 0)
        goto end;

    if ((ret = open_input_file2(argv[2], &inAudioFmtCtx)) < 0)
        goto end;

    /* ========== ALLOCATE OUTPUT CONTEXT ==========*/

    avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, out_filename);
    if (!outFmtCtx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ofmt = outFmtCtx->oformat;

    /* =============== SETUP VIDEO CODEC ================*/
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

    /* =============== SETUP AUDIO CODEC ================*/
    for (i = 0; i < inAudioFmtCtx->nb_streams; i++) {
        setup_mp3_audio_codec(outFmtCtx);
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


    /* =============== SETUP FILTERS ================*/

    init_filters(inAudioFmtCtx, outFmtCtx);

    AVStream *in_stream, *out_stream;
    AVFrame* frame;

    while (1) {        

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
        pkt.duration = (int)av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(outFmtCtx, &pkt, "out");

        ret = av_interleaved_write_frame(outFmtCtx, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_free_packet(&pkt);


        /* =============== AUDIO STREAM ================*/

#if 0
        ret = av_read_frame(inAudioFmtCtx, &pkt);
        if (ret < 0)
            break;
        in_stream = inAudioFmtCtx->streams[pkt.stream_index];

        pkt.stream_index++;
        out_stream = outFmtCtx->streams[pkt.stream_index];

        log_packet(inAudioFmtCtx, &pkt, "in");

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
#else

        if ((ret = av_read_frame(inAudioFmtCtx, &pkt)) < 0)
            break;
        int streamIndex = pkt.stream_index;
        int gotFrame;

        if (_filterCtx[streamIndex].FilterGraph) {
            av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");
            frame = av_frame_alloc();
            if (!frame) {
                ret = AVERROR(ENOMEM);
                break;
            }
            av_packet_rescale_ts(&pkt, inAudioFmtCtx->streams[streamIndex]->time_base,
                inAudioFmtCtx->streams[streamIndex]->codec->time_base);
            ret = avcodec_decode_audio4(inAudioFmtCtx->streams[streamIndex]->codec, frame,
                &gotFrame, &pkt);
            if (ret < 0) {
                av_frame_free(&frame);
                av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                break;
            }

            if (gotFrame) {
                frame->pts = av_frame_get_best_effort_timestamp(frame);
                ret = filter_encode_write_frame(frame, inAudioFmtCtx, outFmtCtx, streamIndex);
                av_frame_free(&frame);
                if (ret < 0)
                    goto end;
            }
            else {
                av_frame_free(&frame);
            }
        }
        else {
            /* remux this frame without reencoding */
            av_packet_rescale_ts(&pkt,
                inAudioFmtCtx->streams[streamIndex]->time_base,
                outFmtCtx->streams[streamIndex+1]->time_base);

            ret = av_interleaved_write_frame(outFmtCtx, &pkt);
            if (ret < 0)
                goto end;
        }
        av_free_packet(&pkt);
#endif // 0

    }

    av_write_trailer(outFmtCtx);
end:

    av_free_packet(&pkt);
    av_frame_free(&frame);
    for (i = 0; i < inAudioFmtCtx->nb_streams; i++) {
        avcodec_close(inAudioFmtCtx->streams[i]->codec);
        if (outFmtCtx && outFmtCtx->nb_streams > i && outFmtCtx->streams[i] && outFmtCtx->streams[i]->codec)
            avcodec_close(outFmtCtx->streams[i]->codec);
        if (_filterCtx && _filterCtx[i].FilterGraph)
            avfilter_graph_free(&_filterCtx[i].FilterGraph);
    }
    av_free(_filterCtx);

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