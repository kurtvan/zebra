#include "io/VideoSource.hpp"

#include "common/log.hpp"
#include "io/macos/RectToTexture.hpp"

#import <OpenGL/OpenGL.h>
#import <Syphon/Syphon.h>

namespace gv::io {

namespace {

constexpr const char* kLogCategory = "syphon";
constexpr int kReconnectIntervalFrames = 30; //!< Look for the server about twice a second while disconnected.

auto ToStdString(NSString* string) -> std::string
{
    return string != nil ? std::string(string.UTF8String) : std::string();
}

auto Matches(NSDictionary* description, const std::string& appName, const std::string& serverName) -> bool
{
    const std::string app = ToStdString(description[SyphonServerDescriptionAppNameKey]);
    const std::string name = ToStdString(description[SyphonServerDescriptionNameKey]);
    return (appName.empty() || app == appName) && (serverName.empty() || name == serverName);
}

class SyphonVideoSource final : public VideoSource
{
public:
    SyphonVideoSource(std::string appName, std::string serverName)
        : m_appName(std::move(appName))
        , m_serverName(std::move(serverName))
    {
        m_context = CGLGetCurrentContext();
        TryConnect();
    }

    ~SyphonVideoSource() override
    {
        [m_client stop];
        m_client = nil;
    }

    /// Called every frame for sources the upcoming scenes need: keeps looking for the server, so it's
    /// connected before the scene goes live rather than after.
    void SetActive(bool active) override
    {
        if (active && (m_client == nil || !m_client.isValid))
        {
            DropIfInvalid();
            if (++m_framesSinceAttempt >= kReconnectIntervalFrames)
                TryConnect();
        }
    }

    auto Update() -> GLuint override
    {
        if (m_client == nil || !m_client.isValid)
        {
            DropIfInvalid();
            if (++m_framesSinceAttempt >= kReconnectIntervalFrames)
            {
                TryConnect();
            }
            return 0; // never a stale frame from a server that went away
        }

        if (m_client.hasNewFrame)
        {
            SyphonOpenGLImage* image = [m_client newFrameImage];
            if (image != nil)
            {
                const NSSize size = image.textureSize;
                // Syphon frames are already bottom-up.
                m_converter.Convert(image.textureName, static_cast<int>(size.width), static_cast<int>(size.height), false);
                m_hasFrame = true;
                m_state = VideoState::Live;
            }
        }
        return m_hasFrame ? m_converter.Current() : 0;
    }

    auto State() const -> VideoState override
    {
        return m_state;
    }

    auto Describe() const -> std::string override
    {
        std::string text = "Syphon ";
        text += m_appName.empty() ? "(any app)" : m_appName;
        if (!m_serverName.empty())
            text += " / " + m_serverName;
        return text;
    }

private:
    void DropIfInvalid()
    {
        if (m_client != nil && !m_client.isValid)
        {
            GV_LOG_WARN(kLogCategory, "%s went away", Describe().c_str());
            [m_client stop];
            m_client = nil;
        }
        if (m_client == nil)
        {
            m_state = VideoState::Disconnected;
            m_hasFrame = false;
        }
    }

    void TryConnect()
    {
        m_framesSinceAttempt = 0;
        if (m_context == nullptr)
            return;
        for (NSDictionary* description in [[SyphonServerDirectory sharedDirectory] servers])
        {
            if (Matches(description, m_appName, m_serverName))
            {
                m_client = [[SyphonOpenGLClient alloc] initWithServerDescription:description context:m_context options:nil newFrameHandler:nil];
                if (m_client != nil && m_client.isValid)
                {
                    GV_LOG_INFO(kLogCategory, "connected to %s", Describe().c_str());
                    m_state = VideoState::Starting;
                    return;
                }
                m_client = nil;
            }
        }
        m_state = VideoState::Disconnected;
    }

    std::string m_appName;
    std::string m_serverName;
    CGLContextObj m_context{nullptr};
    SyphonOpenGLClient* m_client{nil};
    RectToTexture m_converter;
    VideoState m_state{VideoState::Disconnected};
    bool m_hasFrame{false};
    int m_framesSinceAttempt{0};
};

} // namespace

auto CreateSyphonVideoSource(const std::string& appName, const std::string& serverName) -> std::unique_ptr<VideoSource>
{
    return std::make_unique<SyphonVideoSource>(appName, serverName);
}

auto ListSyphonServers() -> std::vector<SyphonServerInfo>
{
    std::vector<SyphonServerInfo> servers;
    for (NSDictionary* description in [[SyphonServerDirectory sharedDirectory] servers])
    {
        servers.push_back({ToStdString(description[SyphonServerDescriptionAppNameKey]), ToStdString(description[SyphonServerDescriptionNameKey])});
    }
    return servers;
}

} // namespace gv::io
