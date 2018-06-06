#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include "libavutil/log.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_log.h"

/**
 * This program ONLY displays the video via SDL
 *
 * If you're reading this, then you should know that this code is a
 * proof-of-concept, and I uploaded it just to get it out there. Some of the
 * comments are outdated, naming scheme could be better, etc., but I want to
 * post it just because I can and someone might find it a good starter point.
 *
 * clang -o sdl_video sdl_video.c -lavutil -lavformat -lavcodec -lswscale -lSDL2 -Wno-deprecated-declarations
 *
 * Things this tiny little proof-of-concept DOES do:
 * - plays back the MPEG2 filename I have hardcoded
 * - correctly uses libav and SDL2 to display the video
 *
 * This this tiny little proof-of-concept DOES NOT do:
 * - play audio
 * - look at timing on video
 *
 */

int main(int argc, char **argv) {

	int ix = 0;
	int width = 0;
	int height = 0;
	int video_frame_ix = 0;
	int audio_frame_ix = 0;
	int retval = -1;
	int got_picture = -1;
	int got_frame_ptr = -1;
	int64_t pts = 0;
	int64_t dts = 0;

	char media_filename[] = "video.vob";
	int video_stream_ix = 1;
	int audio_stream_ix = 2;

	SDL_Init(SDL_INIT_VIDEO);
	
	/**
	 * Debugging
	 *
	 * Start by setting the debug level.
	 *
	 * The most reasonable options: AV_LOG_INFO, AV_LOG_VERBOSE, and AV_LOG_DEBUG
	 *
	 * See libavutil/log.h
	 */
	av_log_set_level(AV_LOG_DEBUG);

	/**
	 * Initialization
	 *
	 * Load *all* the formats, codecs, etc. that libav supports. I could load
	 * the specific ones I want, but finding them is difficult, and goes down
	 * the rabbit hole. This is good enough.
	 *
	 * The alternative here would be to use av_register_input_format() for the
	 * container.
	 *
	 * See lavformat/avformat.h
	 */
	av_register_all();

	/**
	 * File (format i/o context)
	 *
	 * https://ffmpeg.org/doxygen/3.4/structAVFormatContext.html
	 *
	 * This is where the file first begins to be accessed. It's a pointer
	 * to everything in the data. It won't actually be opened until later.
	 *
	 * I'm setting the data file here manually so as to keep the code
	 * smaller.
	 *
	 * Also part of lavformat/avformat.h
	 *
	 * Here I'm naming it media_file so I know where it's getting its data
	 * from anytime it's accessed.
	 *
	 * Like a lot of these that are pointers, it needs to be initialized
	 * using internal libav functions.
	 *
	 * Increase the probesize so it can scan a VOB, and run
	 * avformat_find_stream_info as MPEG2 is specifically one listed in the
	 * documentation as not having headers.
	 *
	 * It may make more sense to call the context a container.
	 *
	 */
	AVFormatContext *media_file = NULL;
	media_file = avformat_alloc_context();
	media_file->max_analyze_duration = media_file->probesize * 10;
	avformat_open_input(&media_file, media_filename, NULL, NULL);
	avformat_find_stream_info(media_file, NULL);

	// Display format information
	// With debug level enabled, it will show you the # of frames and its timing index
	// av_dump_format(media_file, 0, media_filename, 0);

	/**
	 * Format information
	 *
	 * The AVInputFormat will hold information about the container,
	 * which is useful if you want to print out metadata.
	 *
	 * video.vob - MPEG-PS (MPEG-2 Program Stream)
	 * video.mpg - MPEG-PS (MPEG-2 Program Stream)
	 * video.m2ts - MPEG-TS (MPEG-2 Transport Stream)
	 * video.mp4 - QuickTime / MOV
	 * video.y4m - YUV4MPEG pipe
	 * video.x264 - raw H.264 video
	 * video.x265 - raw HEVC video
	 * audio.ac3 - raw AC-3
	 * audio.dts - raw DTS
	 *
	 */
	/*
	AVInputFormat *media_info = NULL;
	media_info = media_file->iformat;
	printf("%s\n", format_info->long_name);
	*/

	/**
	 * Audio / video streams
	 *
	 * The streams themselves are still part of the muxed file, and so the
	 * avformat functions access those as well. Create types here for
	 * each stream.
	 *
	 * A VOB has a DVD NAV packet stream in here, which I'm going to
	 * ignore, and skip straight to the audio and video ones. Normally
	 * I'd loop through them to see if they match what I want:
	 *
	 * if(media_file->streams[1]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
	 * 	video_stream = media_file->streams[1];
	 *
	 * Think of this in context of I need to copy a *stream* from one
	 * container to another, as in an index, ignoring the codec that it
	 * actually is.
	 *
	 * https://ffmpeg.org/doxygen/3.4/structAVStream.html
	 *
	 */

	AVStream *video_stream = NULL;
	AVStream *audio_stream = NULL;

	video_stream = media_file->streams[video_stream_ix];
	audio_stream = media_file->streams[audio_stream_ix];

	/**
	 * Codec parameters
	 *
	 * Probably most of the data you're looking for will be in 'codecpar',
	 * so it's important to know where it is and what it has.
	 *
	 * Since it has so many details, that's what I call it.
	 *
	 * https://ffmpeg.org/doxygen/3.4/structAVStream.html#a12826d21779289356722971d362c583c
	 */
	AVCodecParameters *video_stream_details = NULL;
	AVCodecParameters *audio_stream_details = NULL;

	video_stream_details = video_stream->codecpar;
	audio_stream_details = audio_stream->codecpar;

	width = video_stream_details->width;
	height = video_stream_details->height;

	/*
	 * The stream has the media type, which you could check when looking
	 * at the streams to see if it has what you want.
	 *
	 * See https://ffmpeg.org/doxygen/3.4/group__lavu__misc.html#ga9a84bba4713dfced21a1a56163be1f48
	 * Examples: AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, AVMEDIA_TYPE_SUBTITLE
	 *
	 */
	enum AVMediaType video_stream_type;
	enum AVMediaType audio_stream_type;
	enum AVMediaType media_stream_type;

	video_stream_type = video_stream_details->codec_type;
	audio_stream_type = audio_stream_details->codec_type;

	/**
	 * The codec ID is also stored in the video stream, which you will
	 * use when loading the codec information.
	 *
	 * Examples: AV_CODEC_ID_MPEG2VIDEO, AV_CODEC_ID_AC3, AV_CODEC_ID_DTS,
	 * AV_CODEC_ID_DVD_SUBTITLE, AV_CODEC_ID_DVD_NAV
	 *
	 * https://ffmpeg.org/doxygen/3.4/group__lavc__core.html#gaadca229ad2c20e060a14fec08a5cc7ce
	 */
	enum AVCodecID video_codec_id;
	enum AVCodecID audio_codec_id;

	video_codec_id = video_stream_details->codec_id;
	audio_codec_id = audio_stream_details->codec_id;

	/**
	 * Codec Info
	 *
	 * This stores information about the codec, such as what its
	 * capabilities are.
	 *
	 * Use the codec id from before to load it.
	 *
	 * https://ffmpeg.org/doxygen/3.4/structAVCodec.html
	 */
	AVCodec *video_codec_info = NULL;
	AVCodec *audio_codec_info = NULL;

	video_codec_info = avcodec_find_decoder(video_codec_id);
	audio_codec_info = avcodec_find_decoder(audio_codec_id);

	/**
	 * Codec context
	 *
	 * The codec context is the container for the data in the stream. It
	 * is secondary to the media file container. This is where everything
	 * is found.
	 *
	 * It is this codec context that all the data is pulled from.
	 *
	 * Make one for the video stream, and one for an audio stream.
	 *
	 * Naming it "video" and "audio" seems to be reasonable.
	 *
	 * The context types are pointers that need to be initialized as well. ???? why is this here???
	 *
	 * https://ffmpeg.org/doxygen/3.4/structAVCodecContext.html
	 *
	 */
	AVCodecContext *video_codec_context = NULL;
	AVCodecContext *audio_codec_context = NULL;

	video_codec_context = media_file->streams[video_stream_ix]->codec;
	audio_codec_context = media_file->streams[audio_stream_ix]->codec;

	avcodec_open2(video_codec_context, video_codec_info, NULL);
	avcodec_open2(audio_codec_context, audio_codec_info, NULL);

	/**
	 * Compressed data
	 *
	 * The AVPackat type contains a part of the compressed data in the
	 * media stream. You would loop through this data and then send it to a
	 * decoder to do something with it. For now, you just look at it here
	 * unless you want to do something with it.
	 *
	 * The packet itself will contain data that you can examine that can
	 * also influence what you want to do next, such as the byte position
	 * in the stream.
	 *
	 * Since the word packet is somewhat confusing, I'll call it data.
	 *
	 * https://ffmpeg.org/doxygen/3.4/structAVPacket.html
	 *
	 */
	// AVPacket *video_packet = NULL; // unused, I use media_packet right now
	AVPacket *audio_packet = NULL;
	AVPacket *media_packet = NULL;

	// video_packet = av_packet_alloc(); // unused, I use media_packet right now
	audio_packet = av_packet_alloc();
	media_packet = av_packet_alloc();

	/**
	 * Audio and video frames
	 *
	 * Create some frames for data from the uncompressed packets to go
	 * into, one for audio and one for video.
	 *
	 * Also a third one for reading data directly from the media file
	 * as a placeholder.
	 *
	 * https://ffmpeg.org/doxygen/3.4/structAVFrame.html
	 */
	AVFrame *video_src_frame = NULL;
	AVFrame *video_yuv_frame = NULL;
	AVFrame *audio_frame = NULL;

	video_src_frame = av_frame_alloc();
	video_yuv_frame = av_frame_alloc();
	audio_frame = av_frame_alloc();

	/**
	 * Software scaling
	 *
	 * Needed to convert images. Create a buffer to store video in with the
	 * amount required for our buffer with the sizes of the target video.
	 *
	 * av_image_get_buffer_size():
	 * https://ffmpeg.org/doxygen/3.4/group__lavu__picture.html#ga24a67963c3ae0054a2a4bab35930e694
	 *
	 * https://ffmpeg.org/doxygen/3.4/structSwsContext.html
	 *
	 * sws_getContext():
	 * https://ffmpeg.org/doxygen/3.4/group__libsws.html#gaf360d1a9e0e60f906f74d7d44f9abfdd
	 *
	 */
	int video_buffer_size;
	unsigned char *video_buffer = NULL;

	video_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
	video_buffer = av_malloc(video_buffer_size);
	retval = av_image_fill_arrays(video_yuv_frame->data, video_yuv_frame->linesize, video_buffer, AV_PIX_FMT_YUV420P, width, height, 1);

	struct SwsContext *sws_context = NULL;

	sws_context = sws_getContext(width, height, video_codec_context->pix_fmt, width, height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	/**
	 * SDL media player
	 */
	SDL_Window *window = NULL;
        SDL_Renderer *sdl_rend = NULL;
	SDL_Texture *sdl_texture = NULL;
	SDL_Thread *sdl_thread = NULL;

	window = SDL_CreateWindow("MPEG2 Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_MINIMIZED);
        sdl_rend = SDL_CreateRenderer(window, -1, 0);
	sdl_texture = SDL_CreateTexture(sdl_rend, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);

	/**
	 * Sending data to the decoder
	 *
	 * 'send' here is confusing in its function name, as 'decompress' might
	 * make more sense.
	 *
	 * The codec context details are used here as a reference for how much
	 * data is needed.
	 *
	 * Here the data is actually decompressed.
	 *
	 * For video, it's a single frame. For audio, it's multiple frames.
	 *
	 * While the decode_video2 and decode_audio4 functions are deprecated,
	 * there's little documentation or working exaples out there that use
	 * the new ones, which makes me really frustrted. In addition, ffplay
	 * uses these, so I'm sticking with that out of practicality and
	 * maintaining my sanity.
	 *
	 * av_read_frame():
	 * https://ffmpeg.org/doxygen/3.4/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61
	 *
	 * avcodec_decode_video2():
	 * https://ffmpeg.org/doxygen/3.4/group__lavc__decoding.html#ga3ac51525b7ad8bca4ced9f3446e96532
	 *
	 * avcodec_decode_audio4():
	 * https://ffmpeg.org/doxygen/3.4/group__lavc__decoding.html#gaaa1fbe477c04455cdc7a994090100db4
	 *
	 * Each packet needs to be freed when no longer needed to reset
	 * everything.
	 *
	 * av_packet_unref():
	 * https://ffmpeg.org/doxygen/3.4/group__lavc__packet.html#ga63d5a489b419bd5d45cfd09091cbcbc2
	 *
	 */
	while(av_read_frame(media_file, media_packet) == 0) {

		ix = media_packet->stream_index;
		media_stream_type = media_file->streams[ix]->codecpar->codec_type;

		if(media_stream_type != AVMEDIA_TYPE_VIDEO && media_stream_type != AVMEDIA_TYPE_AUDIO) {
			av_packet_unref(media_packet);
			continue;
		}

		if(media_stream_type == AVMEDIA_TYPE_VIDEO) {

			avcodec_decode_video2(video_codec_context, video_src_frame, &got_picture, media_packet);

			if(!got_picture) {
				av_packet_unref(media_packet);
				continue;
			}

			sws_scale(sws_context, (const unsigned char *const *)video_src_frame->data, video_src_frame->linesize, 0, height, video_yuv_frame->data, video_yuv_frame->linesize);

			SDL_UpdateTexture(sdl_texture, NULL, video_yuv_frame->data[0], video_yuv_frame->linesize[0]);
			SDL_RenderClear(sdl_rend);
			SDL_RenderCopy(sdl_rend, sdl_texture, NULL, NULL);
			SDL_RenderPresent(sdl_rend);

			video_frame_ix++;

		}

		if(media_stream_type == AVMEDIA_TYPE_AUDIO) {

			avcodec_decode_audio4(audio_codec_context, audio_frame, &got_frame_ptr, media_packet);

			if(!got_frame_ptr) {
				av_packet_unref(media_packet);
				continue;
			}

			audio_frame_ix++;

		}

		av_packet_unref(media_packet);

		fprintf(stdout, "video frame: %i audio frame: %i\r", video_frame_ix, audio_frame_ix);
		fflush(stdout);

	}

	fprintf(stdout, "\n");

	printf("Width: %i, Height: %i\n", video_stream_details->width, video_stream_details->height);

	return 0;

}
