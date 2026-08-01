#include <tiffio.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
  if(argc != 2) {
    std::cerr << "usage: fixture_map OUTPUT.tif\n";
    return 2;
  }

  constexpr std::uint32_t width = 128;
  constexpr std::uint32_t height = 128;
  TIFF* tiff = TIFFOpen(argv[1], "w");
  if(!tiff) {
    std::cerr << "unable to create " << argv[1] << "\n";
    return 1;
  }

  TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, width);
  TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, height);
  TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 4);
  TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
  TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, height);

  std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 4);
  for(std::uint32_t y = 0; y < height; ++y) {
    for(std::uint32_t x = 0; x < width; ++x) {
      const bool right = x >= width / 2;
      const bool bottom = y >= height / 2;
      const std::size_t offset = static_cast<std::size_t>(x) * 4;
      row[offset] = right ? 40 : 25;
      row[offset + 1] = bottom ? 95 : 45;
      row[offset + 2] = static_cast<std::uint8_t>((x + y) % 32 + 120);
      row[offset + 3] = 255;
    }
    if(TIFFWriteScanline(tiff, row.data(), y, 0) < 0) {
      std::cerr << "failed while writing TIFF row " << y << "\n";
      TIFFClose(tiff);
      return 1;
    }
  }

  TIFFClose(tiff);
  return 0;
}
