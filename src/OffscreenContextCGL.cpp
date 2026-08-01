#include "OffscreenContext.hpp"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

#include <sstream>
#include <stdexcept>

namespace alog2media {
namespace {

std::runtime_error cglError(const std::string& operation, CGLError error) {
  std::ostringstream message;
  message << operation << " failed: " << CGLErrorString(error)
          << " (" << static_cast<int>(error) << ")";
  return std::runtime_error(message.str());
}

class CglContext final : public OffscreenContext {
 public:
  CglContext(int width, int height) {
    CGLPixelFormatAttribute attributes[] = {
      kCGLPFAOpenGLProfile,
      static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_Legacy),
      kCGLPFAColorSize,
      static_cast<CGLPixelFormatAttribute>(24),
      kCGLPFAAlphaSize,
      static_cast<CGLPixelFormatAttribute>(8),
      kCGLPFADepthSize,
      static_cast<CGLPixelFormatAttribute>(24),
      static_cast<CGLPixelFormatAttribute>(0)
    };

    GLint count = 0;
    CGLError error = CGLChoosePixelFormat(attributes, &pixel_format_, &count);
    if(error != kCGLNoError)
      throw cglError("CGLChoosePixelFormat", error);
    if(!pixel_format_ || count == 0)
      throw std::runtime_error("CGL did not provide a legacy OpenGL pixel format");

    error = CGLCreateContext(pixel_format_, nullptr, &context_);
    if(error != kCGLNoError)
      throw cglError("CGLCreateContext", error);

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
      message << "CGL framebuffer is incomplete: 0x" << std::hex << status;
      throw std::runtime_error(message.str());
    }

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
  }

  ~CglContext() override {
    if(context_)
      CGLSetCurrentContext(context_);
    if(depth_buffer_)
      glDeleteRenderbuffers(1, &depth_buffer_);
    if(color_buffer_)
      glDeleteRenderbuffers(1, &color_buffer_);
    if(framebuffer_)
      glDeleteFramebuffers(1, &framebuffer_);
    CGLSetCurrentContext(nullptr);
    if(context_)
      CGLDestroyContext(context_);
    if(pixel_format_)
      CGLDestroyPixelFormat(pixel_format_);
  }

  void makeCurrent() override {
    const CGLError error = CGLSetCurrentContext(context_);
    if(error != kCGLNoError)
      throw cglError("CGLSetCurrentContext", error);
    if(framebuffer_)
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  }

  std::string name() const override { return "cgl-fbo"; }

 private:
  CGLPixelFormatObj pixel_format_ = nullptr;
  CGLContextObj context_ = nullptr;
  GLuint framebuffer_ = 0;
  GLuint color_buffer_ = 0;
  GLuint depth_buffer_ = 0;
};

}  // namespace

std::unique_ptr<OffscreenContext> makeOffscreenContext(int width, int height) {
  return std::make_unique<CglContext>(width, height);
}

}  // namespace alog2media
