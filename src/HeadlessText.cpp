#include <FL/gl.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TextRenderer {
 public:
  TextRenderer() {
    if(FT_Init_FreeType(&library_) != 0)
      throw std::runtime_error("FreeType initialization failed");

    const std::string font = findFont();
    if(font.empty())
      throw std::runtime_error(
          "no bold sans-serif font found; set ALOG2MEDIA_FONT to a .ttf/.ttc file");
    if(FT_New_Face(library_, font.c_str(), 0, &face_) != 0)
      throw std::runtime_error("FreeType could not open headless render font: " + font);
    setSize(12);
  }

  ~TextRenderer() {
    if(face_)
      FT_Done_Face(face_);
    if(library_)
      FT_Done_FreeType(library_);
  }

  void setSize(int pixels) {
    pixels = std::max(1, pixels);
    if(pixels == pixel_size_)
      return;
    if(FT_Set_Pixel_Sizes(face_, 0, static_cast<FT_UInt>(pixels)) != 0)
      throw std::runtime_error("FreeType could not select the requested font size");
    pixel_size_ = pixels;
  }

  void draw(const char* text) {
    if(!text || !*text)
      return;

    GLboolean raster_valid = GL_FALSE;
    GLfloat color[4] = {1, 1, 1, 1};
    GLint unpack_alignment = 4;
    glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID, &raster_valid);
    if(!raster_valid)
      return;
    glGetFloatv(GL_CURRENT_COLOR, color);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_PIXEL_MODE_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for(const unsigned char* current =
            reinterpret_cast<const unsigned char*>(text);
        *current; ++current) {
      unsigned long codepoint = *current;
      if(codepoint >= 128)
        codepoint = '?';
      if(FT_Load_Char(face_, codepoint, FT_LOAD_RENDER) != 0)
        continue;

      const FT_GlyphSlot glyph = face_->glyph;
      const int width = static_cast<int>(glyph->bitmap.width);
      const int rows = static_cast<int>(glyph->bitmap.rows);
      const float advance = static_cast<float>(glyph->advance.x) / 64.0F;
      if(width > 0 && rows > 0) {
        std::vector<unsigned char> rgba(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(rows) * 4);
        for(int destination_row = 0; destination_row < rows; ++destination_row) {
          const int source_row = rows - destination_row - 1;
          const unsigned char* source =
              glyph->bitmap.buffer + source_row * glyph->bitmap.pitch;
          for(int column = 0; column < width; ++column) {
            const std::size_t offset =
                (static_cast<std::size_t>(destination_row) * width + column) * 4;
            rgba[offset] = static_cast<unsigned char>(
                std::clamp(color[0], 0.0F, 1.0F) * 255.0F);
            rgba[offset + 1] = static_cast<unsigned char>(
                std::clamp(color[1], 0.0F, 1.0F) * 255.0F);
            rgba[offset + 2] = static_cast<unsigned char>(
                std::clamp(color[2], 0.0F, 1.0F) * 255.0F);
            rgba[offset + 3] = static_cast<unsigned char>(
                static_cast<float>(source[column]) *
                std::clamp(color[3], 0.0F, 1.0F));
          }
        }

        // Move relative to MarineViewer's already transformed raster position.
        // A zero-size glBitmap changes only that position and is available in
        // every legacy OpenGL context supported by the renderer.
        glBitmap(0, 0, 0, 0, static_cast<float>(glyph->bitmap_left),
                 static_cast<float>(glyph->bitmap_top - rows), nullptr);
        glDrawPixels(width, rows, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBitmap(0, 0, 0, 0,
                 advance - static_cast<float>(glyph->bitmap_left),
                 static_cast<float>(rows - glyph->bitmap_top), nullptr);
      } else {
        glBitmap(0, 0, 0, 0, advance, 0, nullptr);
      }
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
    glPopAttrib();
  }

 private:
  static std::string findFont() {
    if(const char* configured = std::getenv("ALOG2MEDIA_FONT")) {
      if(std::filesystem::is_regular_file(configured))
        return configured;
    }

    const std::array<const char*, 6> candidates = {
      "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
      "/System/Library/Fonts/HelveticaNeue.ttc",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
      "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"
    };
    for(const char* candidate : candidates) {
      if(std::filesystem::is_regular_file(candidate))
        return candidate;
    }
    return {};
  }

  FT_Library library_ = nullptr;
  FT_Face face_ = nullptr;
  int pixel_size_ = 0;
};

TextRenderer& renderer() {
  static TextRenderer instance;
  return instance;
}

}  // namespace

// MOOS-IvP's MarineViewer uses only these two FLTK text entry points. Defining
// them in the executable keeps label rendering independent of an Fl_Gl_Window
// driver while preserving the existing viewer call sites.
void gl_font(int, int size) {
  renderer().setSize(size);
}

void gl_draw(const char* text) {
  renderer().draw(text);
}
