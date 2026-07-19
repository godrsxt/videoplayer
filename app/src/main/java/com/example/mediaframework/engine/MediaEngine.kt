package com.example.mediaframework.engine

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import java.lang.ref.WeakReference

class MediaEngine private constructor(context: Context) {
    companion object {
        @Volatile private var INSTANCE: MediaEngine? = null
        fun getInstance(context: Context): MediaEngine = INSTANCE ?: synchronized(this) {
            INSTANCE ?: MediaEngine(context.applicationContext).also { INSTANCE = it }
        }
    }

    private val scope = CoroutineScope(Dispatchers.IO + Job())
    private var nativeHandle: Long = 0
    private var audioTrack: AudioTrack? = null
    private var playbackCallback: PlaybackCallback? = null
    private var isInitialized = false

    init {
        System.loadLibrary("mediaengine")
        nativeHandle = nativeCreate()
    }

    interface PlaybackCallback {
        fun onPrepared()
        fun onStarted()
        fun onPaused()
        fun onStopped()
        fun onSeekComplete(positionMs: Long)
        fun onError(errorCode: Int)
        fun onPositionUpdate(positionMs: Long, durationMs: Long)
    }

    fun initialize(sampleRate: Int = 48000, channels: Int = 2, bufferSize: Int = 1024): Boolean {
        return if (!isInitialized) {
            val result = nativeInitialize(nativeHandle, sampleRate, channels, bufferSize)
            if (result) {
                setupAudioTrack(sampleRate, channels, bufferSize)
                isInitialized = true
            }
            result
        } else true
    }

    private fun setupAudioTrack(sampleRate: Int, channels: Int, bufferSize: Int) {
        val channelConfig = if (channels == 1) AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
        val minBufferSize = AudioTrack.getMinBufferSize(sampleRate, channelConfig, AudioFormat.ENCODING_PCM_FLOAT)
        val bufferSizeFrames = maxOf(bufferSize, minBufferSize / (channels * 4))

        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .build()

        audioTrack = AudioTrack.Builder()
            .setAudioAttributes(attributes)
            .setAudioFormat(AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                .setSampleRate(sampleRate)
                .setChannelMask(channelConfig)
                .build())
            .setBufferSizeInBytes(bufferSizeFrames * channels * 4)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        audioTrack?.play()
    }

    fun setDataSource(path: String) {
        scope.launch {
            nativeSetDataSource(nativeHandle, path)
        }
    }

    fun prepare() {
        scope.launch {
            nativePrepare(nativeHandle)
        }
    }

    fun start() {
        scope.launch {
            nativeStart(nativeHandle)
            audioTrack?.play()
            playbackCallback?.onStarted()
        }
    }

    fun pause() {
        scope.launch {
            nativePause(nativeHandle)
            audioTrack?.pause()
            playbackCallback?.onPaused()
        }
    }

    fun stop() {
        scope.launch {
            nativeStop(nativeHandle)
            audioTrack?.stop()
            playbackCallback?.onStopped()
        }
    }

    fun seekTo(positionMs: Long) {
        scope.launch {
            nativeSeekTo(nativeHandle, positionMs)
        }
    }

    fun getCurrentPosition(): Long = nativeGetCurrentPosition(nativeHandle)

    fun getDuration(): Long = nativeGetDuration(nativeHandle)

    fun setVolume(volume: Float) {
        nativeSetVolume(nativeHandle, volume.coerceIn(0f, 2f))
        audioTrack?.setVolume(volume.coerceIn(0f, 1f))
    }

    fun setPlaybackSpeed(speed: Float) {
        nativeSetPlaybackSpeed(nativeHandle, speed.coerceIn(0.5f, 2f))
    }

    fun enableLoudnessEnhancer(enabled: Boolean, gainMillibels: Float = 6000f) {
        nativeEnableLoudnessEnhancer(nativeHandle, enabled, gainMillibels)
    }

    fun setPlaybackCallback(callback: PlaybackCallback?) {
        playbackCallback = callback
    }

    fun release() {
        scope.launch {
            nativeRelease(nativeHandle)
            nativeDestroy(nativeHandle)
            audioTrack?.release()
            audioTrack = null
            scope.cancel()
            isInitialized = false
        }
    }

    external fun nativeCreate(): Long
    external fun nativeDestroy(handle: Long)
    external fun nativeInitialize(handle: Long, sampleRate: Int, channels: Int, bufferSize: Int): Boolean
    external fun nativeSetDataSource(handle: Long, path: String)
    external fun nativePrepare(handle: Long)
    external fun nativeStart(handle: Long)
    external fun nativePause(handle: Long)
    external fun nativeStop(handle: Long)
    external fun nativeSeekTo(handle: Long, positionMs: Long)
    external fun nativeGetCurrentPosition(handle: Long): Long
    external fun nativeGetDuration(handle: Long): Long
    external fun nativeSetVolume(handle: Long, volume: Float)
    external fun nativeSetPlaybackSpeed(handle: Long, speed: Float)
    external fun nativeEnableLoudnessEnhancer(handle: Long, enabled: Boolean, gainMillibels: Float)
    external fun nativeRelease(handle: Long)
}