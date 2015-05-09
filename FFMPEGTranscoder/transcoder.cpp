/*
* Copyright (c) 2010 Nicolas George
* Copyright (c) 2011 Stefano Sabatini
* Copyright (c) 2014 Andrey Utkin
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
* API example for demuxing, decoding, filtering, encoding and muxing
* @example transcoding.c
*/
#include "makefile.h"
#ifdef TRANSCODING


#if _MSC_VER
#define snprintf _snprintf_s
#endif
#define __STDC_CONSTANT_MACROS 
#define __STDC_FORMAT_MACROS

extern "C" 
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include "opencv2/core/core.hpp"


static AVFormatContext* _inputFormatCtx;
static AVFormatContext* _outputFormatCtx;
typedef struct FilteringContext {
    AVFilterContext* BuffersinkCtx;
    AVFilterContext* BuffersrcCtx;
    AVFilterGraph* FilterGraph;
} FilteringContext;
static FilteringContext* _filterCtx;

static int open_input_file(const char *filename)
{
    int ret;
    unsigned int i;

    _inputFormatCtx = NULL;
    if ((ret = avformat_open_input(&_inputFormatCtx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(_inputFormatCtx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    for (i = 0; i < _inputFormatCtx->nb_streams; i++) {
        AVStream *stream;
        AVCodecContext *codecCtx;
        stream = _inputFormatCtx->streams[i];
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

    av_dump_format(_inputFormatCtx, 0, filename, 0);
    return 0;
}

static int open_output_file(const char *filename)
{
    AVStream *outStream;
    AVStream *inStream;
    AVCodecContext *decoderCtx, *encoderCtx;
    AVCodec *encoder;
    int ret;
    unsigned int i;

    _outputFormatCtx = NULL;
    avformat_alloc_output_context2(&_outputFormatCtx, NULL, NULL, filename);
    if (!_outputFormatCtx) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    }


    for (i = 0; i < _inputFormatCtx->nb_streams; i++) {
        outStream = avformat_new_stream(_outputFormatCtx, NULL);
        if (!outStream) {
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }

        inStream = _inputFormatCtx->streams[i];
        decoderCtx = inStream->codec;
        encoderCtx = outStream->codec;

        if (decoderCtx->codec_type == AVMEDIA_TYPE_VIDEO
            || decoderCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* in this example, we choose transcoding to same codec */
            //encoder = avcodec_find_encoder(dec_ctx->codec_id);
            encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
            if (!encoder) {
                av_log(NULL, AV_LOG_FATAL, "Neccessary encoder not found\n");
                return AVERROR_INVALIDDATA;
            }

            /* In this example, we transcode to same properties (picture size,
            * sample rate etc.). These properties can be changed for output
            * streams easily using filters */
            if (decoderCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
                encoderCtx->height = decoderCtx->height;
                encoderCtx->width = decoderCtx->width;
                encoderCtx->sample_aspect_ratio = decoderCtx->sample_aspect_ratio;
                /* take first format from list of supported formats */
                encoderCtx->pix_fmt = encoder->pix_fmts[0];
                /* video time_base can be set to whatever is handy and supported by encoder */
                encoderCtx->time_base = decoderCtx->time_base;
            }
            else {
                encoderCtx->sample_rate = decoderCtx->sample_rate;
                encoderCtx->channel_layout = AV_CH_LAYOUT_STEREO;
                encoderCtx->channels = av_get_channel_layout_nb_channels(encoderCtx->channel_layout);
                /* take first format from list of supported formats */
                encoderCtx->sample_fmt = encoder->sample_fmts[0];
                encoderCtx->time_base.num = 1;
                encoderCtx->time_base.den = encoderCtx->sample_rate;
                //enc_ctx->time_base = (AVRational){ 1,  };
            }

            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(encoderCtx, encoder, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
                return ret;
            }
        }
        else if (decoderCtx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        }
        else {
            /* if this stream must be remuxed */
            ret = avcodec_copy_context(_outputFormatCtx->streams[i]->codec,
                _inputFormatCtx->streams[i]->codec);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Copying stream context failed\n");
                return ret;
            }
        }

        if (_outputFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
            encoderCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

    }
    av_dump_format(_outputFormatCtx, 0, filename, 1);

    if (!(_outputFormatCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&_outputFormatCtx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(_outputFormatCtx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

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

static int init_filters(void)
{
    const char* filterSpec;
    unsigned int i;
    int ret;
    _filterCtx = (FilteringContext*)av_malloc_array(_inputFormatCtx->nb_streams, sizeof(*_filterCtx));
    if (!_filterCtx)
        return AVERROR(ENOMEM);

    for (i = 0; i < _inputFormatCtx->nb_streams; i++) {
        _filterCtx[i].BuffersrcCtx = NULL;
        _filterCtx[i].BuffersinkCtx = NULL;
        _filterCtx[i].FilterGraph = NULL;
        if (!(_inputFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO
            || _inputFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;

        if (_inputFormatCtx->streams[i]->codec->codec_id == _outputFormatCtx->streams[i]->codec->codec_id)
            continue;

        if (_inputFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            filterSpec = "null"; /* passthrough (dummy) filter for video */
        else
            filterSpec = "anull"; /* passthrough (dummy) filter for audio */
        ret = init_filter(&_filterCtx[i], _inputFormatCtx->streams[i]->codec,
            _outputFormatCtx->streams[i]->codec, filterSpec);
        if (ret)
            return ret;
    }
    return 0;
}

static int encode_write_frame(AVFrame *filt_frame, unsigned int stream_index, int *got_frame) {
    int ret;
    int gotFrameLocal;
    AVPacket encPkt;
    int(*encodeFunc)(AVCodecContext *, AVPacket *, const AVFrame *, int *) =
        (_inputFormatCtx->streams[stream_index]->codec->codec_type ==
        AVMEDIA_TYPE_VIDEO) ? avcodec_encode_video2 : avcodec_encode_audio2;

    if (!got_frame)
        got_frame = &gotFrameLocal;

    av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
    /* encode filtered frame */
    encPkt.data = NULL;
    encPkt.size = 0;
    av_init_packet(&encPkt);
    ret = encodeFunc(_outputFormatCtx->streams[stream_index]->codec, &encPkt,
        filt_frame, got_frame);
    av_frame_free(&filt_frame);
    if (ret < 0)
        return ret;
    if (!(*got_frame))
        return 0;

    /* prepare packet for muxing */
    encPkt.stream_index = stream_index;
    av_packet_rescale_ts(&encPkt,
        _outputFormatCtx->streams[stream_index]->codec->time_base,
        _outputFormatCtx->streams[stream_index]->time_base);

    av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
    /* mux encoded frame */
    ret = av_interleaved_write_frame(_outputFormatCtx, &encPkt);
    return ret;
}

static int filter_encode_write_frame(AVFrame *frame, unsigned int stream_index)
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
        ret = encode_write_frame(filtFrame, stream_index, NULL);
        if (ret < 0)
            break;
    }

    return ret;
}

static int flush_encoder(unsigned int stream_index)
{
    int ret;
    int gotFrame;

    if (!(_outputFormatCtx->streams[stream_index]->codec->codec->capabilities &
        CODEC_CAP_DELAY))
        return 0;

    while (1) {
        av_log(NULL, AV_LOG_INFO, "Flushing stream #%u encoder\n", stream_index);
        ret = encode_write_frame(NULL, stream_index, &gotFrame);
        if (ret < 0)
            break;
        if (!gotFrame)
            return 0;
    }
    return ret;
}

int main(int argc, char **argv)
{
    int ret;
    AVPacket packet;
    packet.data = NULL;
    packet.size = 0;
    AVFrame *frame = NULL;
    enum AVMediaType type;
    unsigned int streamIndex;
    unsigned int i;
    int gotFrame;
    int(*decodeFunc)(AVCodecContext *, AVFrame *, int *, const AVPacket *);

    double startTick, endTick, duration;
    startTick = static_cast<double>(cv::getTickCount());


#ifdef RUN_WITH_ARGS
    if (argc != 3) {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }
#endif // 0


    av_register_all();
    avfilter_register_all();

#if RUN_WITH_ARGS
    if ((ret = open_input_file(argv[1])) < 0)
        goto end;
    if ((ret = open_output_file(argv[2])) < 0)
        goto end;
    if ((ret = init_filters()) < 0)
        goto end;
#else

    if ((ret = open_input_file("panther.wav")) < 0)
        goto end;
    if ((ret = open_output_file("pantherTranscoded_HOME.mp3")) < 0)
        goto end;
    if ((ret = init_filters()) < 0)
        goto end;
#endif // 0


    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(_inputFormatCtx, &packet)) < 0)
            break;
        streamIndex = packet.stream_index;
        type = _inputFormatCtx->streams[packet.stream_index]->codec->codec_type;
        av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u\n",
            streamIndex);

        // TODO: find right condition for reencode and filter (prob codec_id is enough)
        if (_filterCtx[streamIndex].FilterGraph) {
            av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");
            frame = av_frame_alloc();
            if (!frame) {
                ret = AVERROR(ENOMEM);
                break;
            }
            av_packet_rescale_ts(&packet,
                _inputFormatCtx->streams[streamIndex]->time_base,
                _inputFormatCtx->streams[streamIndex]->codec->time_base);
            decodeFunc = (type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 :
                avcodec_decode_audio4;
            ret = decodeFunc(_inputFormatCtx->streams[streamIndex]->codec, frame,
                &gotFrame, &packet);
            if (ret < 0) {
                av_frame_free(&frame);
                av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
                break;
            }

            if (gotFrame) {
                frame->pts = av_frame_get_best_effort_timestamp(frame);
                ret = filter_encode_write_frame(frame, streamIndex);
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
            av_packet_rescale_ts(&packet,
                _inputFormatCtx->streams[streamIndex]->time_base,
                _outputFormatCtx->streams[streamIndex]->time_base);

            ret = av_interleaved_write_frame(_outputFormatCtx, &packet);
            if (ret < 0)
                goto end;
        }
        av_free_packet(&packet);
    }

    /* flush filters and encoders */
    for (i = 0; i < _inputFormatCtx->nb_streams; i++) {
        /* flush filter */
        if (!_filterCtx[i].FilterGraph)
            continue;
        ret = filter_encode_write_frame(NULL, i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing filter failed\n");
            goto end;
        }

        /* flush encoder */
        ret = flush_encoder(i);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
            goto end;
        }
    }

    av_write_trailer(_outputFormatCtx);
end:
    av_free_packet(&packet);
    av_frame_free(&frame);
    for (i = 0; i < _inputFormatCtx->nb_streams; i++) {
        avcodec_close(_inputFormatCtx->streams[i]->codec);
        if (_outputFormatCtx && _outputFormatCtx->nb_streams > i && _outputFormatCtx->streams[i] && _outputFormatCtx->streams[i]->codec)
            avcodec_close(_outputFormatCtx->streams[i]->codec);
        if (_filterCtx && _filterCtx[i].FilterGraph)
            avfilter_graph_free(&_filterCtx[i].FilterGraph);
    }
    av_free(_filterCtx);
    avformat_close_input(&_inputFormatCtx);
    if (_outputFormatCtx && !(_outputFormatCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&_outputFormatCtx->pb);
    avformat_free_context(_outputFormatCtx);

    if (ret < 0)
        //av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
        av_log(NULL, AV_LOG_ERROR, "Error occurred!!!\n");

    endTick = static_cast<double>(cv::getTickCount());
    duration = ((endTick - startTick) / cv::getTickFrequency());
    av_log(NULL, AV_LOG_WARNING, "Time Elapsed: %.2f sec.\n", duration);

    return ret ? 1 : 0;
}
#endif