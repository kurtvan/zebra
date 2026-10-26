#include "io/VideoSource.hpp"

#include "common/log.hpp"
#include "io/macos/RectToTexture.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGL/OpenGL.h>
#import <QuartzCore/QuartzCore.h>

namespace gv::io {

namespace {

constexpr const char* kLogCategory = "video";

class FileVideoSource final : public VideoSource
{
public:
    explicit FileVideoSource(const std::filesystem::path& file)
        : m_file(file)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec))
        {
            m_state = VideoState::Missing;
            GV_LOG_WARN(kLogCategory, "video file not found: %s", file.c_str());
            return;
        }

        CGLContextObj context = CGLGetCurrentContext();
        if (context == nullptr ||
            CVOpenGLTextureCacheCreate(kCFAllocatorDefault, nullptr, context, CGLGetPixelFormat(context), nullptr, &m_cache) != kCVReturnSuccess)
        {
            m_state = VideoState::Failed;
            GV_LOG_ERROR(kLogCategory, "could not create a texture cache for %s", file.c_str());
            return;
        }

        NSDictionary* attributes = @{
            (NSString*) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*) kCVPixelBufferOpenGLCompatibilityKey: @YES,
            (NSString*) kCVPixelBufferIOSurfacePropertiesKey: @{},
        };
        m_output = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attributes];
        AVPlayerItem* item = [AVPlayerItem playerItemWithURL:[NSURL fileURLWithPath:@(file.c_str())]];
        [item addOutput:m_output];
        m_player = [AVPlayer playerWithPlayerItem:item];
        m_player.muted = YES;
        m_player.actionAtItemEnd = AVPlayerActionAtItemEndNone;

        // Loop forever, back to the start point. The observer is removed before this object goes away.
        FileVideoSource* owner = this;
        m_loopObserver = [[NSNotificationCenter defaultCenter] addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                                                                           object:item
                                                                            queue:[NSOperationQueue mainQueue]
                                                                       usingBlock:^(NSNotification*) {
                                                                           owner->SeekToStart();
                                                                       }];
        GV_LOG_INFO(kLogCategory, "opened %s", file.c_str());
    }

    ~FileVideoSource() override
    {
        if (m_loopObserver != nil)
            [[NSNotificationCenter defaultCenter] removeObserver:m_loopObserver];
        [m_player pause];
        m_player = nil;
        m_output = nil;
        if (m_cache != nullptr)
            CFRelease(m_cache);
    }

    auto Update() -> GLuint override
    {
        if (m_player == nil)
        {
            return 0;
        }
        if (m_player.currentItem.status == AVPlayerItemStatusFailed)
        {
            if (m_state != VideoState::Failed)
                GV_LOG_ERROR(kLogCategory, "can't play %s: %s", m_file.c_str(), m_player.currentItem.error.localizedDescription.UTF8String);
            m_state = VideoState::Failed;
            return 0;
        }

        const CMTime time = [m_output itemTimeForHostTime:CACurrentMediaTime()];
        if ([m_output hasNewPixelBufferForItemTime:time])
        {
            CVPixelBufferRef buffer = [m_output copyPixelBufferForItemTime:time itemTimeForDisplay:nullptr];
            if (buffer != nullptr)
            {
                CVOpenGLTextureRef texture = nullptr;
                if (CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_cache, buffer, nullptr, &texture) == kCVReturnSuccess)
                {
                    const int width = static_cast<int>(CVPixelBufferGetWidth(buffer));
                    const int height = static_cast<int>(CVPixelBufferGetHeight(buffer));
                    // CoreVideo textures are usually top-down ("flipped"); the feedback blend wants bottom-up.
                    m_converter.Convert(CVOpenGLTextureGetName(texture), width, height, CVOpenGLTextureIsFlipped(texture));
                    CFRelease(texture);
                    m_state = VideoState::Live;
                }
                CVPixelBufferRelease(buffer);
            }
            CVOpenGLTextureCacheFlush(m_cache, 0);
        }
        return m_converter.Current();
    }

    void SetActive(bool active) override
    {
        if (m_player == nil || active == m_active)
            return;
        m_active = active;
        if (active)
            m_player.rate = static_cast<float>(m_speed);
        else
            [m_player pause];
    }

    void Restart() override
    {
        SeekToStart();
    }

    void SetPlayback(double startSeconds, double speed) override
    {
        const bool startChanged = !m_playbackSet || startSeconds != m_startSeconds;
        m_playbackSet = true;
        m_startSeconds = startSeconds;
        m_speed = speed;
        if (startChanged)
            SeekToStart();
        else if (m_player != nil && m_active)
            m_player.rate = static_cast<float>(m_speed);
    }

    auto DurationSeconds() const -> double override
    {
        if (m_player == nil)
            return 0.0;
        const CMTime duration = m_player.currentItem.duration;
        return CMTIME_IS_NUMERIC(duration) ? CMTimeGetSeconds(duration) : 0.0;
    }

    auto PositionSeconds() const -> double override
    {
        if (m_player == nil)
            return 0.0;
        const CMTime time = m_player.currentTime;
        return CMTIME_IS_NUMERIC(time) ? CMTimeGetSeconds(time) : 0.0;
    }

    auto State() const -> VideoState override
    {
        return m_state;
    }

    auto Describe() const -> std::string override
    {
        return m_file.filename().string();
    }

private:
    void SeekToStart()
    {
        if (m_player == nil)
            return;
        // A start point past the end (or a length not known yet) plays from the beginning.
        const double duration = DurationSeconds();
        const double start = duration > 0.0 && m_startSeconds < duration ? m_startSeconds : (duration > 0.0 ? 0.0 : m_startSeconds);
        [m_player seekToTime:CMTimeMakeWithSeconds(start, 600) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        if (m_active)
            m_player.rate = static_cast<float>(m_speed);
    }

    std::filesystem::path m_file;
    double m_startSeconds{0.0};
    double m_speed{1.0};
    bool m_playbackSet{false};
    VideoState m_state{VideoState::Starting};
    AVPlayer* m_player{nil};
    AVPlayerItemVideoOutput* m_output{nil};
    id m_loopObserver{nil};
    CVOpenGLTextureCacheRef m_cache{nullptr};
    RectToTexture m_converter;
    bool m_active{false};
};

} // namespace

auto CreateFileVideoSource(const std::filesystem::path& file) -> std::unique_ptr<VideoSource>
{
    return std::make_unique<FileVideoSource>(file);
}

auto ToString(VideoState state) -> const char*
{
    switch (state)
    {
        case VideoState::Starting:
            return "starting";
        case VideoState::Live:
            return "live";
        case VideoState::Disconnected:
            return "disconnected";
        case VideoState::Missing:
            return "file missing";
        case VideoState::Failed:
            return "failed";
    }
    return "";
}

} // namespace gv::io
