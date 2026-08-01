#include "OffscreenContext.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <sstream>
#include <stdexcept>

namespace alog2media {
namespace {

std::runtime_error eglError(const std::string& operation) {
  std::ostringstream message;
  message << operation << " failed with EGL error 0x" << std::hex
          << eglGetError();
  return std::runtime_error(message.str());
}

class EglContext final : public OffscreenContext {
 public:
  EglContext(int width, int height) {
    using GetPlatformDisplay =
        EGLDisplay (*)(EGLenum, void*, const EGLint*);
    const auto get_platform_display = reinterpret_cast<GetPlatformDisplay>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if(get_platform_display) {
      display_ = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA,
                                      EGL_DEFAULT_DISPLAY, nullptr);
    }
    if(display_ == EGL_NO_DISPLAY)
      display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(display_ == EGL_NO_DISPLAY)
      throw eglError("eglGetPlatformDisplayEXT/eglGetDisplay");

    EGLint major = 0;
    EGLint minor = 0;
    if(eglInitialize(display_, &major, &minor) != EGL_TRUE)
      throw eglError("eglInitialize");
    if(eglBindAPI(EGL_OPENGL_API) != EGL_TRUE)
      throw eglError("eglBindAPI(EGL_OPENGL_API)");

    const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24,
      EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint config_count = 0;
    if(eglChooseConfig(display_, config_attributes, &config, 1,
                       &config_count) != EGL_TRUE || config_count == 0) {
      throw eglError("eglChooseConfig");
    }

    const EGLint surface_attributes[] = {
      EGL_WIDTH, width,
      EGL_HEIGHT, height,
      EGL_NONE
    };
    surface_ = eglCreatePbufferSurface(display_, config, surface_attributes);
    if(surface_ == EGL_NO_SURFACE)
      throw eglError("eglCreatePbufferSurface");

    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, nullptr);
    if(context_ == EGL_NO_CONTEXT)
      throw eglError("eglCreateContext");
    makeCurrent();

    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);

    glGenRenderbuffers(1, &color_buffer_);
    glBindRenderbuffer(GL_RENDERBUFFER, color_buffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, color_buffer_);

    glGenRenderbuffers(1, &depth_buffer_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_buffer_);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE) {
      std::ostringstream message;
      message << "EGL framebuffer is incomplete: 0x" << std::hex << status;
      throw std::runtime_error(message.str());
    }
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
  }

  ~EglContext() override {
    if(display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT)
      eglMakeCurrent(display_, surface_, surface_, context_);
    if(depth_buffer_)
      glDeleteRenderbuffers(1, &depth_buffer_);
    if(color_buffer_)
      glDeleteRenderbuffers(1, &color_buffer_);
    if(framebuffer_)
      glDeleteFramebuffers(1, &framebuffer_);
    if(display_ != EGL_NO_DISPLAY)
      eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if(display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT)
      eglDestroyContext(display_, context_);
    if(display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE)
      eglDestroySurface(display_, surface_);
    if(display_ != EGL_NO_DISPLAY)
      eglTerminate(display_);
  }

  void makeCurrent() override {
    if(eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE)
      throw eglError("eglMakeCurrent");
    if(framebuffer_)
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  }

  std::string name() const override { return "egl-surfaceless-fbo"; }

 private:
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLSurface surface_ = EGL_NO_SURFACE;
  EGLContext context_ = EGL_NO_CONTEXT;
  GLuint framebuffer_ = 0;
  GLuint color_buffer_ = 0;
  GLuint depth_buffer_ = 0;
};

}  // namespace

std::unique_ptr<OffscreenContext> makeOffscreenContext(int width, int height) {
  return std::make_unique<EglContext>(width, height);
}

}  // namespace alog2media
