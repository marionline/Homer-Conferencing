/*****************************************************************************
 *
 * Copyright (C) 2008-2011 Homer-conferencing project
 *
 * This software is free software.
 * Your are allowed to redistribute it and/or modify it under the terms of
 * the GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * This source is published in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License version 2
 * along with this program. Otherwise, you can write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 * Alternatively, you find an online version of the license text under
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 *****************************************************************************/

/*
 * Name:    MediaSourceFile.cpp
 * Purpose: Implementation of a ffmpeg based local file source
 * Author:  Thomas Volkert
 * Since:   2009-02-24
 * Version: $Id$
 */

#include <MediaSourceFile.h>
#include <ProcessStatisticService.h>
#include <Logger.h>
#include <HBThread.h>

#include <string>
#include <unistd.h>

namespace Homer { namespace Multimedia {

using namespace std;
using namespace Homer::Monitor;

///////////////////////////////////////////////////////////////////////////////

MediaSourceFile::MediaSourceFile(string pSourceFile, bool pGrabInRealTime):
    MediaSource("FILE: " + pSourceFile)
{
    mDesiredDevice = pSourceFile;
    mGrabInRealTime = pGrabInRealTime;
    mDuration = 0;
    mCurPts = 0;
    mCurrentDeviceName = "FILE: " + mDesiredDevice;;
}

MediaSourceFile::~MediaSourceFile()
{
    if (mMediaSourceOpened)
        CloseGrabDevice();
}

///////////////////////////////////////////////////////////////////////////////

void MediaSourceFile::getVideoDevices(VideoDevicesList &pVList)
{
    VideoDeviceDescriptor tDevice;

    tDevice.Name = "FILE: " + mDesiredDevice;
    tDevice.Card = mDesiredDevice;
    tDevice.Desc = "file source: \"" + mDesiredDevice + "\"";

    pVList.push_back(tDevice);
}

void MediaSourceFile::getAudioDevices(AudioDevicesList &pAList)
{
    AudioDeviceDescriptor tDevice;

    tDevice.Name = "FILE: " + mDesiredDevice;
    tDevice.Card = mDesiredDevice;
    tDevice.Desc = "file source: \"" + mDesiredDevice + "\"";
    tDevice.IoType = "Input/Output";

    pAList.push_back(tDevice);
}

bool MediaSourceFile::OpenVideoGrabDevice(int pResX, int pResY, float pFps)
{
    FILE                *tFile;
    int                 tResult = 0;
    AVCodec             *tCodec;
    AVInputFormat       *tFormat;
    AVFormatParameters  tFormatParams;

    LOG(LOG_VERBOSE, "Trying to open the video source");

    if (mMediaType == MEDIA_AUDIO)
    {
        LOG(LOG_ERROR, "Wrong media type detected");
        return false;
    }

    SVC_PROCESS_STATISTIC.AssignThreadName("Video-Grabber(FILE)");

    // set category for packet statistics
    ClassifyStream(DATA_TYPE_VIDEO, PACKET_TYPE_RAW);

    if (mMediaSourceOpened)
        return false;

    tFormatParams.channel = 0;
    tFormatParams.standard = NULL;
    tFormatParams.time_base.num = 100;
    tFormatParams.time_base.den = (int)pFps * 100;
    LOG(LOG_VERBOSE, "Desired video time_base: %d/%d (%3.2f)", tFormatParams.time_base.den, tFormatParams.time_base.num, pFps);
    tFormatParams.width = pResX;
    tFormatParams.height = pResY;
    tFormatParams.initial_pause = 0;
    tFormatParams.prealloced_context = 0;

    if (mDesiredDevice.substr(mDesiredDevice.find_last_of(".") + 1) == "mkv")
        tFormat = av_find_input_format("matroska");
    else
    if (mDesiredDevice.substr(mDesiredDevice.find_last_of(".") + 1) == "mpg")
        tFormat = av_find_input_format("mpeg");
    else
        tFormat = av_find_input_format(mDesiredDevice.substr(mDesiredDevice.find_last_of(".") + 1).c_str());
    if (tFormat == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find video input format");
        return false;
    }
    tFormat->flags &= ~AVFMT_NOFILE; // make sure this is marked as file based input

    // Open video file
    // open file and close it again to prevent FFMPEG from crashes, risk of race conditions!
    if (((tFile = fopen(mDesiredDevice.c_str(), "r+")) == NULL) || (fclose(tFile) != 0) || ((tResult = av_open_input_file(&mFormatContext, mDesiredDevice.c_str(), tFormat, 0, &tFormatParams)) != 0))
    {
        LOG(LOG_ERROR, "Couldn't open video file \"%s\" because of \"%s\".", mDesiredDevice.c_str(), strerror(AVUNERROR(tResult)));
        return false;
    }
    mCurrentDevice = mDesiredDevice;

    // limit frame analyzing time for ffmpeg internal codec auto detection
    mFormatContext->max_analyze_duration = AV_TIME_BASE / 4; //  1/4 recorded seconds
    // verbose timestamp debugging    mFormatContext->debug = FF_FDEBUG_TS;

    // Retrieve stream information
    if ((tResult = av_find_stream_info(mFormatContext)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't find video stream information because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the video file
        av_close_input_file(mFormatContext);
        return false;
    }

    mFormatContext->preload= (int)(5 * AV_TIME_BASE); // 0.5 seconds
    mFormatContext->max_delay= (int)(5.5 * AV_TIME_BASE); // 0.7 seconds

    // Find the first video stream
    mMediaStreamIndex = -1;
    for (int i = 0; i < (int)mFormatContext->nb_streams; i++)
    {
        if(mFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            mMediaStreamIndex = i;
            break;
        }
    }
    if (mMediaStreamIndex == -1)
    {
        LOG(LOG_ERROR, "Couldn't find a video stream");
        // Close the video file
        av_close_input_file(mFormatContext);
        return false;
    }

    // Dump information about device file
    dump_format(mFormatContext, mMediaStreamIndex, "MediaSourceFile(video)", false);
    //printf("    ..video stream found with ID: %d, number of available streams: %d\n", mMediaStreamIndex, mFormatContext->nb_streams);

    // Get a pointer to the codec context for the video stream
    mCodecContext = mFormatContext->streams[mMediaStreamIndex]->codec;

    // set grabbing resolution and frame-rate to the resulting ones delivered by opened video codec
    mSourceResX = mCodecContext->width;
    mSourceResY = mCodecContext->height;
    mFrameRate = (float)mFormatContext->streams[mMediaStreamIndex]->time_base.den / mFormatContext->streams[mMediaStreamIndex]->time_base.num;
    LOG(LOG_VERBOSE, "Frame rate determined: %f", mFrameRate);

    if ((mSourceResX == 0) || (mSourceResY == 0))
    {
        LOG(LOG_ERROR, "Couldn't find resolution information within video file");
        // Close the video file
        av_close_input_file(mFormatContext);
        return false;
    }

    // Find the decoder for the video stream
    if((tCodec = avcodec_find_decoder(mCodecContext->codec_id)) == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find a fitting video codec");
        // Close the video file
        av_close_input_file(mFormatContext);
        return false;
    }

    // Open codec
    if ((tResult = avcodec_open(mCodecContext, tCodec)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't open video codec because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the video file
        av_close_input_file(mFormatContext);
        return false;
    }

    // allocate software scaler context
    mScalerContext = sws_getContext(mCodecContext->width, mCodecContext->height, mCodecContext->pix_fmt, mTargetResX, mTargetResY, PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

    //set duration
    if (mFormatContext->streams[mMediaStreamIndex]->duration > 0)
        mDuration = mFormatContext->streams[mMediaStreamIndex]->duration;
    else
        if (mFormatContext->streams[mMediaStreamIndex]->nb_frames > 0)
            mDuration = mFormatContext->streams[mMediaStreamIndex]->nb_frames;
        else
            if (mFormatContext->duration > 0)
                mDuration = mFormatContext->duration;
            else
                mDuration = 0;

    mCurPts = 0;
    mSeekingToPos = true;
    mMediaType = MEDIA_VIDEO;
    MarkOpenGrabDeviceSuccessful();

    return true;
}

bool MediaSourceFile::OpenAudioGrabDevice(int pSampleRate, bool pStereo)
{
    FILE                *tFile;
    int                 tResult = 0;
    AVCodec             *tCodec;
    AVInputFormat       *tFormat;
    AVFormatParameters  tFormatParams;

    LOG(LOG_VERBOSE, "Trying to open the audio source");

    if (mMediaType == MEDIA_VIDEO)
    {
        LOG(LOG_ERROR, "Wrong media type detected");
        return false;
    }

    SVC_PROCESS_STATISTIC.AssignThreadName("Audio-Grabber(FILE)");

    // set category for packet statistics
    ClassifyStream(DATA_TYPE_AUDIO, PACKET_TYPE_RAW);

    if (mMediaSourceOpened)
        return false;

    tFormatParams.sample_rate = pSampleRate; // sampling rate
    tFormatParams.channels = pStereo?2:1; // stereo?
    tFormatParams.initial_pause = 0;
    tFormatParams.prealloced_context = 0;

    if ((mDesiredDevice.substr(mDesiredDevice.find_last_of(".") + 1) == "mkv") || (mDesiredDevice.substr(mDesiredDevice.find_last_of(".") + 1) == "mka"))
        tFormat = av_find_input_format("matroska");
    else
        tFormat = av_find_input_format(mDesiredDevice.substr(mDesiredDevice.find_last_of(".") + 1).c_str());
    if (tFormat == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find audio input format");
        return false;
    }
    tFormat->flags &= ~AVFMT_NOFILE; // make sure this is marked as file based input

    // Open audio file
    // open file and close it again to prevent FFMPEG from crashes, risk of race conditions!
    if (((tFile = fopen(mDesiredDevice.c_str(), "r+")) == NULL) || (fclose(tFile) != 0) || ((tResult = av_open_input_file(&mFormatContext, mDesiredDevice.c_str(), tFormat, 0, &tFormatParams)) != 0))
    {
        LOG(LOG_ERROR, "Couldn't open audio file \"%s\" because of \"%s\".", mDesiredDevice.c_str(), strerror(AVUNERROR(tResult)));
        return false;
    }
    mCurrentDevice = mDesiredDevice;

    // limit frame analyzing time for ffmpeg internal codec auto detection
    mFormatContext->max_analyze_duration = AV_TIME_BASE / 4; //  1/4 recorded seconds
    // verbose timestamp debugging    mFormatContext->debug = FF_FDEBUG_TS;

    // Retrieve stream information
    if ((tResult = av_find_stream_info(mFormatContext)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't find audio stream information because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the audio file
        av_close_input_file(mFormatContext);
        return false;
    }

    mFormatContext->preload= (int)(5 * AV_TIME_BASE); // 0.5 seconds
    mFormatContext->max_delay= (int)(5.5 * AV_TIME_BASE); // 0.7 seconds

    // Find the first audio stream
    mMediaStreamIndex = -1;
    for (int i = 0; i < (int)mFormatContext->nb_streams; i++)
    {
        if(mFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            mMediaStreamIndex = i;
            break;
        }
    }
    if (mMediaStreamIndex == -1)
    {
        LOG(LOG_ERROR, "Couldn't find an audio stream");
        // Close the audio file
        av_close_input_file(mFormatContext);
        return false;
    }

    // Dump information about device file
    dump_format(mFormatContext, mMediaStreamIndex, "MediaSourceFile(audio)", false);
    //printf("    ..audio stream found with ID: %d, number of available streams: %d\n", mMediaStreamIndex, mFormatContext->nb_streams);

    // Get a pointer to the codec context for the audio stream
    mCodecContext = mFormatContext->streams[mMediaStreamIndex]->codec;

    // set sample rate and bit rate to the resulting ones delivered by opened audio codec
    mSampleRate = mCodecContext->sample_rate;
    mStereo = pStereo;
    if(mCodecContext->sample_rate != 44100)
        LOG(LOG_ERROR, "Audio sample rate doesn't match with the one of the output stream and resampling isn't supported yet, output will be digital crap");

    // set rate of incoming frames
    mFrameRate = (float)mFormatContext->streams[mMediaStreamIndex]->time_base.den / mFormatContext->streams[mMediaStreamIndex]->time_base.num;
    LOG(LOG_VERBOSE, "Frame rate determined: %f", mFrameRate);

    // Find the decoder for the audio stream
    if((tCodec = avcodec_find_decoder(mCodecContext->codec_id)) == NULL)
    {
        LOG(LOG_ERROR, "Couldn't find a fitting audio codec");
        // Close the audio file
        av_close_input_file(mFormatContext);
        return false;
    }

    // Inform the codec that we can handle truncated bitstreams
    // bitstreams where sample boundaries can fall in the middle of packets
    if(tCodec->capabilities & CODEC_CAP_TRUNCATED)
        mCodecContext->flags |= CODEC_FLAG_TRUNCATED;

    // Open codec
    if ((tResult = avcodec_open(mCodecContext, tCodec)) < 0)
    {
        LOG(LOG_ERROR, "Couldn't open audio codec because of \"%s\".", strerror(AVUNERROR(tResult)));
        // Close the audio file
        av_close_input_file(mFormatContext);
        return false;
    }

    //set duration
    if (mFormatContext->streams[mMediaStreamIndex]->duration > 0)
        mDuration = mFormatContext->streams[mMediaStreamIndex]->duration;
    else
        if (mFormatContext->streams[mMediaStreamIndex]->nb_frames > 0)
            mDuration = mFormatContext->streams[mMediaStreamIndex]->nb_frames;
        else
            if (mFormatContext->duration > 0)
                mDuration = mFormatContext->duration;
            else
                mDuration = 0;

    mCurPts = 0;
    mSeekingToPos = true;
    mMediaType = MEDIA_AUDIO;
    MarkOpenGrabDeviceSuccessful();

    return true;
}

bool MediaSourceFile::CloseGrabDevice()
{
    bool tResult = false;

    LOG(LOG_VERBOSE, "Going to close, media type is \"%s\"", GetMediaTypeStr().c_str());

    if (mMediaSourceOpened)
    {
        mMediaSourceOpened = false;

        // free the software scaler context
        if (mMediaType == MEDIA_VIDEO)
            sws_freeContext(mScalerContext);

        // Close the codec
        avcodec_close(mCodecContext);

        // Close the file
        av_close_input_file(mFormatContext);

        LOG(LOG_INFO, "...closed, media type is \"%s\"", GetMediaTypeStr().c_str());

        tResult = true;
    }else
        LOG(LOG_INFO, "...wasn't open, media type is \"%s\"", GetMediaTypeStr().c_str());

    mGrabbingStopped = false;
    mMediaType = MEDIA_UNKNOWN;

    return tResult;
}

int MediaSourceFile::GrabChunk(void* pChunkBuffer, int& pChunkSize, bool pDropChunk)
{
    AVFrame             *tSourceFrame = NULL, *tRGBFrame = NULL;
    AVPacket            tPacket;
    int                 tFrameFinished = 0;
    int                 tBytesDecoded = 0;
    int                 tRes = 0;

    // lock grabbing
    mGrabMutex.lock();

    if (pChunkBuffer == NULL)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed"
        MarkGrabChunkFailed(GetMediaTypeStr() + " grab buffer is NULL");

        return GRAB_RES_INVALID;
    }

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed"
        MarkGrabChunkFailed(GetMediaTypeStr() + " source is closed");

        return GRAB_RES_INVALID;
    }

    if (mGrabbingStopped)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge failed"
        MarkGrabChunkFailed(GetMediaTypeStr() + " source is paused");

        return GRAB_RES_INVALID;
    }

    if (mCurPts == mDuration)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        // acknowledge "failed"
        MarkGrabChunkFailed(GetMediaTypeStr() + " seek position equals EOF");

        return GRAB_RES_EOF;
    }

    // Read new packet
    // return 0 if OK, < 0 if error or end of file.
    bool tFrameShouldBeDropped;
    do
    {
        tFrameShouldBeDropped = false;

        // read next sample from source - blocking
        if ((tRes = av_read_frame(mFormatContext, &tPacket)) != 0)
        {
            // unlock grabbing
            mGrabMutex.unlock();

            if ((!mGrabbingStopped) && (tRes != AVERROR_EOF) && (tRes != AVERROR(EIO)))
                LOG(LOG_ERROR, "Couldn't grab a frame because of \"%s\"(%d), media type is \"%s\"", strerror(AVUNERROR(tRes)), tRes, GetMediaTypeStr().c_str());

            if (tRes == AVERROR_EOF)
            {
                mCurPts = mDuration;

                // acknowledge failed"
                MarkGrabChunkFailed(GetMediaTypeStr() + " source has EOF reached");

                return GRAB_RES_EOF;
            }
            if (tRes == AVERROR(EIO))
            {
                // acknowledge failed"
                MarkGrabChunkFailed(GetMediaTypeStr() + " source has I/O error");

                // signal EOF instead of I/O error
                return GRAB_RES_EOF;
            }
            return GRAB_RES_INVALID;
        }
        // is "presentation timestamp" stored within media file?
        if (tPacket.pts == (int64_t)AV_NOPTS_VALUE)
        {// pts isn't stored in the media file, fall back to "decompression timestamp"
            mCurPts = tPacket.dts;
        }else
        {// pts is stored in the media file, use it
            mCurPts = tPacket.pts;
        }

        #if MSF_FRAME_DROP_THRESHOLD > 0
			// #########################################
			// frame dropping
			//      video: seek to start, drop during play out
			//      audio: seek to start, no dropping during play out
			// #########################################
			if (mGrabInRealTime)
			{
				// should we initiate the StartPts value?
				if (mSourceStartPts == -1)
					mSourceStartPts = mCurPts;
				else
				{
					int64_t tRelativePacketTimeUSecs, tRelativeRealTimeUSecs, tDiffPtsUSecs;
					double tPacketRealPtsTmp = 1000000 * (mCurPts - mSourceStartPts) / mFrameRate; // current presentation time in useconds of the current packet from the file source
					tRelativePacketTimeUSecs = (int64_t)tPacketRealPtsTmp;
					tRelativeRealTimeUSecs = av_gettime() - mStartPtsUSecs;
					tDiffPtsUSecs = tRelativePacketTimeUSecs - tRelativeRealTimeUSecs;
					#ifdef MSF_DEBUG_PACKETS
						LOG(LOG_VERBOSE, "Current pts: %8ld (Fps: %3.2f) Stream start pts: %6ld Rel. Packet pts: %8ld Diff time: %8ld us", tRelativeRealTimeUSecs, mFrameRate, mSourceStartPts, tRelativePacketTimeUSecs, tDiffPtsUSecs);
					#endif
					if ((tDiffPtsUSecs < -MSF_FRAME_DROP_THRESHOLD) && ((mMediaType == MEDIA_VIDEO) || (mSeekingToPos)))
					{
						++mChunkNumber;
						if (!mSeekingToPos)
						{
							++mChunkDropCounter;
							#ifdef MSF_DEBUG_PACKETS
								LOG(LOG_VERBOSE, "System too busy (delay > %dms), read frame time diff.: %dms, frame is dropped (%d/%d dropped), media type %d", MSF_FRAME_DROP_THRESHOLD / 1000, -tDiffPtsUSecs / 1000, mChunkDropCounter, mChunkNumber, mMediaType);
							#endif
						}
						tFrameShouldBeDropped = true;
					}
				}
				mSeekingToPos = false;
			}
		#endif
    }while ((tPacket.stream_index != mMediaStreamIndex) || (tFrameShouldBeDropped));

    #ifdef MSF_DEBUG_PACKETS
        LOG(LOG_VERBOSE, "New read chunk %5d with size: %d and stream index: %d, media type %d", mMediaType, mChunkNumber + 1, tPacket.size, tPacket.stream_index);
    #endif

    if ((tPacket.data != NULL) && (tPacket.size > 0))
    {
        #ifdef MSF_DEBUG_PACKETS
            LOG(LOG_VERBOSE, "New packet..");
            LOG(LOG_VERBOSE, "      ..duration: %d", tPacket.duration);
            LOG(LOG_VERBOSE, "      ..pts: %ld stream [%d] pts: %ld", tPacket.pts, mMediaStreamIndex, mFormatContext->streams[mMediaStreamIndex]->pts.val);
            LOG(LOG_VERBOSE, "      ..dts: %ld", tPacket.dts);
            //LOG(LOG_VERBOSE, "      ..size: %d", tPacket.size);
            LOG(LOG_VERBOSE, "      ..pos: %ld", tPacket.pos);
        #endif

        // #########################################
        // process packet
        // #########################################
        switch(mMediaType)
        {
            case MEDIA_VIDEO:
                    if ((!pDropChunk) || (mRecording))
                    {
                        // log statistics
                        AnnouncePacket(tPacket.size);
    //                        #ifdef MSF_DEBUG_PACKETS
    //                            LOG(LOG_VERBOSE, "Decode video frame..");
    //                        #endif

                        // Allocate video frame for source and RGB format
                        if ((tSourceFrame = avcodec_alloc_frame()) == NULL)
                        {
                            // unlock grabbing
                            mGrabMutex.unlock();

                            // acknowledge failed"
                            MarkGrabChunkFailed("out of video memory");

                            return GRAB_RES_INVALID;
                        }

                        // Decode the next chunk of data
                        #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 21, 0)
                            tBytesDecoded = avcodec_decode_video(mCodecContext, tSourceFrame, &tFrameFinished, tPacket.data, tPacket.size);
                        #else
                            tBytesDecoded = avcodec_decode_video2(mCodecContext, tSourceFrame, &tFrameFinished, &tPacket);
                        #endif

                        // transfer the presentation time value
                        tSourceFrame->pts = mCurPts;

    //                        #ifdef MSF_DEBUG_PACKETS
    //                            LOG(LOG_VERBOSE, "    ..with result(!= 0 => OK): %d bytes: %i\n", tFrameFinished, tBytesDecoded);
    //                        #endif

                        // log lost packets: difference between currently received frame number and the number of locally processed frames
                        SetLostPacketCount(tSourceFrame->coded_picture_number - mChunkNumber);

    //                        #ifdef MSF_DEBUG_PACKETS
    //                            LOG(LOG_VERBOSE, "Video frame coded: %d internal frame number: %d", tSourceFrame->coded_picture_number, mChunkNumber);
    //                        #endif

                        // hint: 32 bytes additional data per line within ffmpeg
                        int tCurrentFrameResX = (tSourceFrame->linesize[0] - 32);

                        // check if video resolution has changed within remote GUI
//                        if ((tCurrentFrameResX != -32) && (mSourceResX != 0) && (mSourceResX != tCurrentFrameResX))
//                        {
//                            LOG(LOG_INFO, "Video resolution change within file stream detected (old width: %d new width: %d)", mSourceResX, tCurrentFrameResX);
//
//                            GrabResolutions::iterator tIt, tItEnd = mSupportedVideoFormats.end();
//                            bool tFound = false;
//
//                            for (tIt = mSupportedVideoFormats.begin(); tIt != tItEnd; tIt++)
//                            {
//                                if (tIt->ResX == tCurrentFrameResX)
//                                {
//                                    tFound = true;
//                                    break;
//                                }
//                            }
//
//                            if (tFound)
//                            {
//                                // set grabbing resolution to the resulting ones delivered by received frame
//                                mSourceResX = tIt->ResX;
//                                mCodecContext->width = tIt->ResX;
//                                mSourceResY = tIt->ResY;
//                                mCodecContext->height = tIt->ResY;
//
//                                DoSetVideoGrabResolution(mTargetResX, mTargetResY);
//
//                                LOG(LOG_INFO, "Video resolution changed to \"%s\"(%d * %d)", tIt->Name.c_str(), tIt->ResX, tIt->ResY);
//                            }else
//                                LOG(LOG_ERROR, "Video resolution changed to unknown width: %d", tCurrentFrameResX);
//                        }
                    }

                    // re-encode the frame and write it to file
                    if (mRecording)
                        RecordFrame(tSourceFrame);

                    if (!pDropChunk)
                    {
    //                        #ifdef MSF_DEBUG_PACKETS
    //                            LOG(LOG_VERBOSE, "Convert video frame..");
    //                        #endif

                        // Allocate video frame for RGB format
                        if ((tRGBFrame = avcodec_alloc_frame()) == NULL)
                        {
                            // Free the YUV frame
                            if (tSourceFrame != NULL)
                                av_free(tSourceFrame);

                            // free packet buffer
                            av_free_packet(&tPacket);

                            // unlock grabbing
                            mGrabMutex.unlock();

                            // acknowledge failed"
                            MarkGrabChunkFailed("out of audio memory");

                            return GRAB_RES_INVALID;
                        }

                        // Assign appropriate parts of buffer to image planes in tRGBFrame
                        avpicture_fill((AVPicture *)tRGBFrame, (uint8_t *)pChunkBuffer, PIX_FMT_RGB32, mTargetResX, mTargetResY);

                        // convert frame from YUV to RGB format
                        if ((tFrameFinished != 0) && (tBytesDecoded >= 0))
                        {
                            HM_sws_scale(mScalerContext, tSourceFrame->data, tSourceFrame->linesize, 0, mCodecContext->height, tRGBFrame->data, tRGBFrame->linesize);
                        }else
                        {
                            // Free the RGB frame
                            av_free(tRGBFrame);

                            // Free the YUV frame
                            if (tSourceFrame != NULL)
                                av_free(tSourceFrame);

                            // free packet buffer
                            av_free_packet(&tPacket);

                            // unlock grabbing
                            mGrabMutex.unlock();

                            // only print debug output if it is not "operation not permitted"
                            //if ((tBytesDecoded < 0) && (AVUNERROR(tBytesDecoded) != EPERM))
                            // acknowledge failed"
                            MarkGrabChunkFailed("couldn't decode video frame-" + toString(strerror(AVUNERROR(tBytesDecoded))) + "(" + toString(AVUNERROR(tBytesDecoded)) + ")");

                            return GRAB_RES_INVALID;
                        }
                        #ifdef MSF_DEBUG_PACKETS
                            LOG(LOG_VERBOSE, "New video frame..");
                            LOG(LOG_VERBOSE, "      ..key frame: %d", tSourceFrame->key_frame);
                            switch(tSourceFrame->pict_type)
                            {
                                    case FF_I_TYPE:
                                        LOG(LOG_VERBOSE, "      ..picture type: i-frame");
                                        break;
                                    case FF_P_TYPE:
                                        LOG(LOG_VERBOSE, "      ..picture type: p-frame");
                                        break;
                                    case FF_B_TYPE:
                                        LOG(LOG_VERBOSE, "      ..picture type: b-frame");
                                        break;
                                    default:
                                        LOG(LOG_VERBOSE, "      ..picture type: %d", tSourceFrame->pict_type);
                                        break;
                            }
                            LOG(LOG_VERBOSE, "      ..pts: %ld", tSourceFrame->pts);
                            LOG(LOG_VERBOSE, "      ..coded pic number: %d", tSourceFrame->coded_picture_number);
                            LOG(LOG_VERBOSE, "      ..display pic number: %d", tSourceFrame->display_picture_number);
                        #endif

                        // return size of decoded frame
                        pChunkSize = avpicture_get_size(PIX_FMT_RGB32, mTargetResX, mTargetResY) * sizeof(uint8_t);

                        // Free the RGB frame
                        av_free(tRGBFrame);
                    }

                    // Free the YUV frame
                    if (tSourceFrame != NULL)
                        av_free(tSourceFrame);

                    break;

            case MEDIA_AUDIO:
                    if ((!pDropChunk) || (mRecording))
                    {
                        // log statistics
                        AnnouncePacket(tPacket.size);

                        //printf("DecodeFrame..\n");
                        // Decode the next chunk of data
                        int tOutputBufferSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
                        #ifdef MSF_DEBUG_PACKETS
                            LOG(LOG_VERBOSE, "Decoding audio samples into buffer of size: %d", tOutputBufferSize);
                        #endif

                        #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 21, 0)
                            int tBytesDecoded = avcodec_decode_audio2(mCodecContext, (int16_t *)pChunkBuffer, &tOutputBufferSize, tPacket.data, tPacket.size);
                        #else
                            int tBytesDecoded = avcodec_decode_audio3(mCodecContext, (int16_t *)pChunkBuffer, &tOutputBufferSize, &tPacket);
                        #endif
                        pChunkSize = tOutputBufferSize;
                        #ifdef MSF_DEBUG_PACKETS
                            LOG(LOG_VERBOSE, "Result is an audio frame with size of %d bytes from %d encoded bytes", tOutputBufferSize, tBytesDecoded);
                        #endif

                        if ((tBytesDecoded < 0) || (tOutputBufferSize == 0))
                        {
                            // free packet buffer
                            av_free_packet(&tPacket);

                            // unlock grabbing
                            mGrabMutex.unlock();

                            // only print debug output if it is not "operation not permitted"
                            //if (AVUNERROR(tBytesDecoded) != EPERM)
                            // acknowledge failed"
                            MarkGrabChunkFailed("couldn't decode audio frame-" + toString(strerror(AVUNERROR(tBytesDecoded))) + "(" + toString(AVUNERROR(tBytesDecoded)) + ")");

                            return GRAB_RES_INVALID;
                        }
                    }
                    break;

            default:
                    LOG(LOG_ERROR, "Media type unknown");
                    break;

        }
        // free packet buffer
        av_free_packet(&tPacket);
    }else
        LOG(LOG_ERROR, "Empty packet received, media type is \"%s\"", GetMediaTypeStr().c_str());

    // unlock grabbing
    mGrabMutex.unlock();

    // #########################################
    // frame rate emulation
    // #########################################
    // inspired by "output_packet" from ffmpeg.c
    if (mGrabInRealTime)
    {
        // should we initiate the StartPts value?
        if (mSourceStartPts == -1)
            mSourceStartPts = mCurPts;
        else
        {
            int64_t tRelativePacketTimeUSecs, tRelativeRealTimeUSecs, tDiffPtsUSecs;
            double tPacketRealPtsTmp = 1000000 * (mCurPts - mSourceStartPts) / mFrameRate; // current presentation time in useconds of the current packet from the file source
            tRelativePacketTimeUSecs = (int64_t)tPacketRealPtsTmp;
            tRelativeRealTimeUSecs = av_gettime() - mStartPtsUSecs;
            tDiffPtsUSecs = tRelativePacketTimeUSecs - tRelativeRealTimeUSecs;
            #ifdef MSF_DEBUG_TIMING
                LOG(LOG_VERBOSE, "Current pts: %8lld (Fps: %3.2f) Stream start pts: %6lld Rel. Packet pts: %8lld Diff time: %8lld us", tRelativeRealTimeUSecs, mFrameRate, mSourceStartPts, tRelativePacketTimeUSecs, tDiffPtsUSecs);
            #endif
            // adapt timing to real-time, ignore timings between -5 ms and +5 ms
            if (tDiffPtsUSecs > 0)
            {
                #ifdef MSF_DEBUG_TIMING
                    LOG(LOG_VERBOSE, "  Sleeping for %d us, media type: %d", tDiffPtsUSecs, mMediaType);
                #endif
				Thread::Suspend(tDiffPtsUSecs);
            }else
            {
                #ifdef MSF_DEBUG_TIMING
                    if (tDiffPtsUSecs < -MSF_FRAME_DROP_THRESHOLD)
                        LOG(LOG_VERBOSE, "System too busy (delay > %dms), decoded frame time diff.: %dms, media type %d", MSF_FRAME_DROP_THRESHOLD / 1000, -tDiffPtsUSecs / 1000, mMediaType);
                #endif
            }
        }
    }

    // acknowledge success
    MarkGrabChunkSuccessful();

    return ++mChunkNumber;
}

void MediaSourceFile::DoSetVideoGrabResolution(int pResX, int pResY)
{
    if ((mMediaSourceOpened) && (mScalerContext != NULL))
    {
        // free the software scaler context
        sws_freeContext(mScalerContext);

        // allocate software scaler context
        mScalerContext = sws_getContext(mCodecContext->width, mCodecContext->height, mCodecContext->pix_fmt, pResX, pResY, PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
    }
}

GrabResolutions MediaSourceFile::GetSupportedVideoGrabResolutions()
{
    VideoFormatDescriptor tFormat;

    mSupportedVideoFormats.clear();

    if (mMediaType == MEDIA_VIDEO)
    {
        tFormat.Name="SQCIF";      //      128 �  96
        tFormat.ResX = 128;
        tFormat.ResY = 96;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="QCIF";       //      176 � 144
        tFormat.ResX = 176;
        tFormat.ResY = 144;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="CIF";        //      352 � 288
        tFormat.ResX = 352;
        tFormat.ResY = 288;
        mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="VGA";       //
        tFormat.ResX = 640;
        tFormat.ResY = 480;
        mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="CIF4";       //      704 � 576
        tFormat.ResX = 704;
        tFormat.ResY = 576;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="SVGA";       //
        tFormat.ResX = 800;
        tFormat.ResY = 600;
        mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="XGA";       //
        tFormat.ResX = 1024;
        tFormat.ResY = 768;
        mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="CIF9";       //     1056 � 864
        tFormat.ResX = 1056;
        tFormat.ResY = 864;
        //mSupportedVideoFormats.push_back(tFormat);

        tFormat.Name="CIF16";      //     1408 � 1152
        tFormat.ResX = 1408;
        tFormat.ResY = 1152;
        //mSupportedVideoFormats.push_back(tFormat);

        if (mMediaSourceOpened)
        {
            tFormat.Name="Original";
            tFormat.ResX = mCodecContext->width;
            tFormat.ResY = mCodecContext->height;
            mSupportedVideoFormats.push_back(tFormat);
        }
    }

    return mSupportedVideoFormats;
}

bool MediaSourceFile::SupportsSeeking()
{
    return true;
}

int64_t MediaSourceFile::GetSeekEnd()
{
    float tResult = 0;

    // lock grabbing
    mGrabMutex.lock();

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        //LOG(LOG_ERROR, "Tried to get seek end while source is closed");
        return 0;
    }

    if ((mFormatContext) && (mFormatContext->streams[mMediaStreamIndex]) && (mFrameRate > 0))
        tResult = (float)mDuration / mFrameRate;
    else
        tResult = 0;

    // unlock grabbing
    mGrabMutex.unlock();

    return (int64_t)tResult;
}

bool MediaSourceFile::Seek(int64_t pSeconds, bool pOnlyKeyFrames)
{
    bool tResult = false;
    int64_t tSeekEnd = GetSeekEnd();

    if ((pSeconds < 0) || (pSeconds > tSeekEnd))
    {
        LOG(LOG_ERROR, "Seek position is out of range (%ld/%ld), media type is \"%s\"", pSeconds, tSeekEnd, GetMediaTypeStr().c_str());
        return false;
    }

    // lock grabbing
    mGrabMutex.lock();

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        //LOG(LOG_ERROR, "Tried to seek while source is closed, media type %d", mMediaType);
        return false;
    }

    //LOG(LOG_VERBOSE, "pSecs: %ld CurPts: %ld", pSeconds, mCurPts);
    int64_t tAbsoluteTimestamp = pSeconds * (int64_t)mFrameRate; // later the value for mCurPts will be changed to the same value like tAbsoluteTimestamp

    //LOG(LOG_VERBOSE, "Rel: %ld Abs: %ld", tRelativeTimestamp, tAbsoluteTimestamp);

    if ((tAbsoluteTimestamp >= 0) && (tAbsoluteTimestamp <= mDuration))
    {
        tResult = (av_seek_frame(mFormatContext, mMediaStreamIndex, tAbsoluteTimestamp, pOnlyKeyFrames ? 0 : AVSEEK_FLAG_ANY) >= 0);

        // adopt the stored pts value which represent the start of the media presentation in real-time useconds
        int64_t tFrameNumber = tAbsoluteTimestamp - mSourceStartPts;
        int64_t tRelativeRealTimeUSecs = 1000000 * tFrameNumber / (int64_t)mFrameRate;
        //LOG(LOG_VERBOSE, "Old start: %ld", mStartPtsUSecs);
        mStartPtsUSecs = av_gettime() - tRelativeRealTimeUSecs;
        //LOG(LOG_VERBOSE, "New start: %ld", mStartPtsUSecs);

        if (!tResult)
            LOG(LOG_ERROR, "Error during relative seeking in source file, media type is \"%s\"", GetMediaTypeStr().c_str());
        else
            mCurPts = tAbsoluteTimestamp;
    }else
        LOG(LOG_ERROR, "Seek position is out of range, media type is \"%s\"", GetMediaTypeStr().c_str());

    // inform about seeking state, don't inform about dropped frames because they are dropped caused by seeking and not by timing problems
    mSeekingToPos = true;

    // unlock grabbing
    mGrabMutex.unlock();

    return tResult;
}

bool MediaSourceFile::SeekRelative(int64_t pSeconds, bool pOnlyKeyFrames)
{
    bool tResult = false;

    // lock grabbing
    mGrabMutex.lock();

    if (!mMediaSourceOpened)
    {
        // unlock grabbing
        mGrabMutex.unlock();

        //LOG(LOG_ERROR, "Tried to seek while source is closed");
        return false;
    }

    //LOG(LOG_VERBOSE, "pSecs: %ld CurPts: %ld", pSeconds, mCurPts);
    int64_t tRelativeTimestamp = pSeconds * (int64_t)mFrameRate;
    int64_t tAbsoluteTimestamp = mCurPts + tRelativeTimestamp; // later the value for mCurPts will be changed to the same value like tAbsoluteTimestamp

    //LOG(LOG_VERBOSE, "Rel: %ld Abs: %ld", tRelativeTimestamp, tAbsoluteTimestamp);

    if ((tAbsoluteTimestamp >= 0) && (tAbsoluteTimestamp <= mDuration))
    {
        tResult = (av_seek_frame(mFormatContext, mMediaStreamIndex, tAbsoluteTimestamp, pOnlyKeyFrames ? 0 : AVSEEK_FLAG_ANY) >= 0);

        // adopt the stored pts value which represent the start of the media presentation in real-time useconds
        int64_t tFrameNumber = tAbsoluteTimestamp - mSourceStartPts;
        int64_t tRelativeRealTimeUSecs = 1000000 * tFrameNumber / (int64_t)mFrameRate;
        //LOG(LOG_VERBOSE, "Old start: %ld", mStartPtsUSecs);
        mStartPtsUSecs = av_gettime() - tRelativeRealTimeUSecs;
        //LOG(LOG_VERBOSE, "New start: %ld", mStartPtsUSecs);

        if (!tResult)
            LOG(LOG_ERROR, "Error during relative seeking in source file, media type is \"%s\"", GetMediaTypeStr().c_str());
        else
            mCurPts = tAbsoluteTimestamp;
    }else
        LOG(LOG_ERROR, "Seek position is out of range, media type is \"%s\"", GetMediaTypeStr().c_str());

    // inform about seeking state, don't inform about dropped frames because they are dropped caused by seeking and not by timing problems
    mSeekingToPos = true;

    // unlock grabbing
    mGrabMutex.unlock();

    return tResult;
}

int64_t MediaSourceFile::GetSeekPos()
{
    return (mCurPts / ((int64_t)mFrameRate));
}

///////////////////////////////////////////////////////////////////////////////

}} //namespace