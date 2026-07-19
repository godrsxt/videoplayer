package com.example.mediaframework.playback

import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata as Media3Metadata
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.MediaSource
import androidx.media3.exoplayer.source.ProgressiveMediaSource
import androidx.media3.exoplayer.upstream.DefaultDataSourceFactory
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import androidx.media3.session.MediaController
import com.example.mediaframework.engine.MediaEngine
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

@UnstableApi
class MediaPlaybackService : MediaSessionService(), MediaEngine.PlaybackCallback {
    private var mediaSession: androidx.media3.session.MediaSession? = null
    private var exoPlayer: ExoPlayer? = null
    private var mediaEngine: MediaEngine? = null
    private var currentMediaItem: MediaItem? = null
    private val scope = CoroutineScope(Dispatchers.IO + Job())
    private val mainHandler = Handler(Looper.getMainLooper())
    private var audioFocusRequest: AudioFocusRequest? = null
    private var hasAudioFocus = false

    override fun onCreate() {
        super.onCreate()
        mediaEngine = MediaEngine.getInstance(this)
        initializeAudioFocus()
        initializeMediaEngine()
    }

    private fun initializeAudioFocus() {
        val audioManager = getSystemService(AUDIO_SERVICE) as AudioManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            audioFocusRequest = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build())
                .setOnAudioFocusChangeListener { focusChange ->
                    handleAudioFocusChange(focusChange)
                }
                .build()
        }
    }

    private fun initializeMediaEngine() {
        scope.launch {
            val initialized = mediaEngine?.initialize() ?: false
            if (initialized) {
                mediaEngine?.setPlaybackCallback(this@MediaPlaybackService)
            }
        }
    }

    private fun requestAudioFocus(): Boolean {
        val audioManager = getSystemService(AUDIO_SERVICE) as AudioManager
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            audioManager.requestAudioFocus(audioFocusRequest!!) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        } else {
            audioManager.requestAudioFocus(
                null,
                AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN
            ) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        }
    }

    private fun abandonAudioFocus() {
        val audioManager = getSystemService(AUDIO_SERVICE) as AudioManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            audioManager.abandonAudioFocusRequest(audioFocusRequest!!)
        } else {
            audioManager.abandonAudioFocus(null)
        }
    }

    private fun handleAudioFocusChange(focusChange: Int) {
        when (focusChange) {
            AudioManager.AUDIOFOCUS_GAIN -> {
                hasAudioFocus = true
                mediaEngine?.setVolume(1.0f)
                if (mediaSession?.player?.playWhenReady != true) {
                    mediaSession?.player?.playWhenReady = true
                }
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                mediaEngine?.setVolume(0.3f)
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT, AudioManager.AUDIOFOCUS_LOSS -> {
                hasAudioFocus = false
                mediaSession?.player?.pause()
            }
        }
    }

    override fun onGetSession(controllerInfo: MediaController.ControllerInfo): androidx.media3.session.MediaSession {
        return mediaSession!!
    }

    override fun onAddMediaItems(controller: MediaController, mediaItems: List<MediaItem>) {
        scope.launch {
            mediaItems.forEach { item ->
                prepareMediaItem(item)
            }
        }
    }

    private fun prepareMediaItem(item: MediaItem) {
        currentMediaItem = item
        val uri = item.mediaId.toUri()
        
        if (uri.scheme == "file" || uri.scheme == "content" || uri.scheme == "asset") {
            val path = when (uri.scheme) {
                "asset" -> "asset://${uri.path}"
                else -> uri.path!!
            }
            
            mediaEngine?.setDataSource(path)
            mediaEngine?.prepare()
        } else {
            prepareExoPlayer(item)
        }
    }

    private fun prepareExoPlayer(item: MediaItem) {
        mainHandler.post {
            exoPlayer = ExoPlayer.Builder(this)
                .build()
                .also { player ->
                    val dataSourceFactory = DefaultDataSourceFactory(this)
                    val mediaSource = ProgressiveMediaSource.Factory(dataSourceFactory).createMediaItem(item)
                    player.setMediaSource(mediaSource)
                    player.prepare()
                    mediaSession = androidx.media3.session.MediaSession.Builder(this, player).build()
                }
        }
    }

    override fun onPlay() {
        if (hasAudioFocus || requestAudioFocus()) {
            mediaEngine?.start()
            exoPlayer?.playWhenReady = true
        }
    }

    override fun onPause() {
        mediaEngine?.pause()
        exoPlayer?.pause()
    }

    override fun onSeekTo(position: Long) {
        mediaEngine?.seekTo(position)
        exoPlayer?.seekTo(position)
    }

    override fun onSetPlaybackSpeed(speed: Float) {
        mediaEngine?.setPlaybackSpeed(speed)
        exoPlayer?.playbackParameters = exoPlayer?.playbackParameters?.withSpeed(speed)
    }

    override fun onSetVolume(volume: Float) {
        mediaEngine?.setVolume(volume)
    }

    override fun onDestroy() {
        scope.launch {
            mediaEngine?.release()
            exoPlayer?.release()
            abandonAudioFocus()
            scope.cancel()
        }
        super.onDestroy()
    }

    // MediaEngine.PlaybackCallback implementation
    override fun onPrepared() {
        mainHandler.post {
            updatePlaybackState(PlaybackState.STATE_BUFFERING)
        }
    }

    override fun onStarted() {
        mainHandler.post {
            updatePlaybackState(PlaybackState.STATE_PLAYING)
            startForegroundService()
        }
    }

    override fun onPaused() {
        mainHandler.post {
            updatePlaybackState(PlaybackState.STATE_PAUSED)
        }
    }

    override fun onStopped() {
        mainHandler.post {
            updatePlaybackState(PlaybackState.STATE_STOPPED)
            stopForeground(true)
        }
    }

    override fun onSeekComplete(positionMs: Long) {
        mainHandler.post {
            mediaSession?.player?.seekTo(positionMs)
        }
    }

    override fun onError(errorCode: Int) {
        mainHandler.post {
            updatePlaybackState(PlaybackState.STATE_ERROR)
        }
    }

    override fun onPositionUpdate(positionMs: Long, durationMs: Long) {
        mainHandler.post {
            val playbackState = PlaybackState.Builder()
                .setState(PlaybackState.STATE_PLAYING, positionMs, 1.0f)
                .setBufferedPosition(durationMs)
                .build()
            mediaSession?.setPlaybackState(playbackState)
        }
    }

    private fun updatePlaybackState(state: Int) {
        val position = mediaEngine?.getCurrentPosition() ?: exoPlayer?.currentPosition ?: 0L
        val duration = mediaEngine?.getDuration() ?: exoPlayer?.duration ?: 0L
        
        val playbackState = PlaybackState.Builder()
            .setState(state, position, 1.0f)
            .setBufferedPosition(duration)
            .setActions(PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or 
                       PlaybackState.ACTION_SEEK_TO or PlaybackState.ACTION_SKIP_TO_NEXT or
                       PlaybackState.ACTION_SKIP_TO_PREVIOUS or PlaybackState.ACTION_SET_PLAYBACK_SPEED)
            .build()
        
        mediaSession?.setPlaybackState(playbackState)
    }

    private fun startForegroundService() {
        val notification = createNotification()
        startForeground(1, notification, FOREGROUND_SERVICE_MEDIA_PLAYBACK)
    }

    private fun createNotification() = android.app.NotificationCompat.Builder(this, "media_channel")
        .setContentTitle(currentMediaItem?.mediaMetadata?.title ?: "Playing")
        .setContentText(currentMediaItem?.mediaMetadata?.artist ?: "Unknown Artist")
        .setSmallIcon(android.R.drawable.ic_media_play)
        .setOngoing(true)
        .build()
}