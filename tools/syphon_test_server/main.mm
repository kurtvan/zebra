// Publishes a test pattern over Syphon (server "GV Test") for checking the app's Syphon input.
// Top half red, bottom half green, with a white bar sweeping left to right.
//
//   gv_syphon_test_server [seconds]   (default 10)

#include <SDL.h>

#import <Foundation/Foundation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#import <Syphon/Syphon.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char* argv[])
{
    const double seconds = argc > 1 ? std::atof(argv[1]) : 10.0;
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow("syphon test", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, context);

    const int width = 320;
    const int height = 180;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    @autoreleasepool
    {
        SyphonOpenGLServer* server = [[SyphonOpenGLServer alloc] initWithName:@"GV Test" context:CGLGetCurrentContext() options:nil];
        std::printf("publishing Syphon server \"GV Test\" for %.0f s\n", seconds);
        std::fflush(stdout);

        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
        const Uint64 start = SDL_GetTicks64();
        for (int frame = 0; SDL_GetTicks64() - start < static_cast<Uint64>(seconds * 1000.0); ++frame)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
            }
            const int bar = (frame * 4) % width;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    unsigned char* p = &pixels[static_cast<size_t>((y * width + x) * 4)];
                    const bool top = y >= height / 2; // GL rows go bottom-up
                    const bool white = x >= bar && x < bar + 8;
                    p[0] = white ? 255 : (top ? 255 : 0);
                    p[1] = white ? 255 : (top ? 0 : 255);
                    p[2] = white ? 255 : 0;
                    p[3] = 255;
                }
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            [server publishFrameTexture:texture textureTarget:GL_TEXTURE_2D imageRegion:NSMakeRect(0, 0, width, height)
                      textureDimensions:NSMakeSize(width, height) flipped:NO];
            SDL_Delay(16);
        }
        [server stop];
    }
    glDeleteTextures(1, &texture);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
