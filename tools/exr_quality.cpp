#include <OpenEXR/ImfRgbaFile.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Image {
  int width = 0;
  int height = 0;
  std::vector<OPENEXR_IMF_NAMESPACE::Rgba> pixels;
};

Image read(const std::filesystem::path& path) {
  // OpenEXR's filename API accepts const char*. On Windows path::c_str() is
  // wchar_t*, so convert at this narrow third-party API boundary instead of
  // leaking platform-specific path types through the quality tool.
  const std::string filename = path.string();
  OPENEXR_IMF_NAMESPACE::RgbaInputFile input(filename.c_str());
  const auto window = input.dataWindow();
  Image image{};
  image.width = window.max.x - window.min.x + 1;
  image.height = window.max.y - window.min.y + 1;
  image.pixels.resize(static_cast<size_t>(image.width) * image.height);
  input.setFrameBuffer(
      image.pixels.data() - window.min.x - static_cast<ptrdiff_t>(window.min.y) * image.width,
      1,
      image.width);
  input.readPixels(window.min.y, window.max.y);
  return image;
}

double luminance(const OPENEXR_IMF_NAMESPACE::Rgba& pixel) {
  return 0.2126 * static_cast<float>(pixel.r) + 0.7152 * static_cast<float>(pixel.g) +
         0.0722 * static_cast<float>(pixel.b);
}

struct Metrics {
  double mse = 0.0;
  double psnr = std::numeric_limits<double>::infinity();
  double ssim = 1.0;
  double maximumError = 0.0;
};

OPENEXR_IMF_NAMESPACE::Rgba sampleBilinear(const Image& image, double u, double v) {
  const double x = std::clamp(u * image.width - 0.5, 0.0, static_cast<double>(image.width - 1));
  const double y = std::clamp(v * image.height - 0.5, 0.0, static_cast<double>(image.height - 1));
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, image.width - 1);
  const int y1 = std::min(y0 + 1, image.height - 1);
  const double tx = x - x0;
  const double ty = y - y0;
  const auto at = [&](int px, int py) -> const OPENEXR_IMF_NAMESPACE::Rgba& {
    return image.pixels[static_cast<size_t>(py) * image.width + px];
  };
  OPENEXR_IMF_NAMESPACE::Rgba result{};
  for (const auto channel :
       {&OPENEXR_IMF_NAMESPACE::Rgba::r,
        &OPENEXR_IMF_NAMESPACE::Rgba::g,
        &OPENEXR_IMF_NAMESPACE::Rgba::b,
        &OPENEXR_IMF_NAMESPACE::Rgba::a}) {
    const double top = (1.0 - tx) * static_cast<float>(at(x0, y0).*channel) +
                       tx * static_cast<float>(at(x1, y0).*channel);
    const double bottom = (1.0 - tx) * static_cast<float>(at(x0, y1).*channel) +
                          tx * static_cast<float>(at(x1, y1).*channel);
    result.*channel = static_cast<float>((1.0 - ty) * top + ty * bottom);
  }
  return result;
}

Image normalizedTo(const Image& source, int width, int height) {
  if (source.width == width && source.height == height) return source;
  Image result{
      width,
      height,
      std::vector<OPENEXR_IMF_NAMESPACE::Rgba>(static_cast<size_t>(width) * height)};
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      result.pixels[static_cast<size_t>(y) * width + x] =
          sampleBilinear(source, (x + 0.5) / width, (y + 0.5) / height);
    }
  }
  return result;
}

Metrics compare(const Image& reference, const Image& test) {
  const Image normalizedTest = normalizedTo(test, reference.width, reference.height);
  const size_t count = reference.pixels.size();
  double meanReference = 0.0;
  double meanTest = 0.0;
  Metrics result{};
  for (size_t index = 0; index < count; ++index) {
    meanReference += luminance(reference.pixels[index]);
    meanTest += luminance(normalizedTest.pixels[index]);
    for (const auto channel :
         {&OPENEXR_IMF_NAMESPACE::Rgba::r,
          &OPENEXR_IMF_NAMESPACE::Rgba::g,
          &OPENEXR_IMF_NAMESPACE::Rgba::b}) {
      const double difference = static_cast<float>(reference.pixels[index].*channel) -
                                static_cast<float>(normalizedTest.pixels[index].*channel);
      result.mse += difference * difference;
      result.maximumError = std::max(result.maximumError, std::abs(difference));
    }
  }
  meanReference /= count;
  meanTest /= count;
  double varianceReference = 0.0;
  double varianceTest = 0.0;
  double covariance = 0.0;
  for (size_t index = 0; index < count; ++index) {
    const double a = luminance(reference.pixels[index]) - meanReference;
    const double b = luminance(normalizedTest.pixels[index]) - meanTest;
    varianceReference += a * a;
    varianceTest += b * b;
    covariance += a * b;
  }
  const double denominator = std::max<double>(count - 1, 1);
  varianceReference /= denominator;
  varianceTest /= denominator;
  covariance /= denominator;
  result.mse /= static_cast<double>(count * 3u);
  const double peak = std::max(
      1.0,
      std::max(meanReference, meanTest) +
          3.0 * std::sqrt(std::max(varianceReference, varianceTest)));
  result.psnr = result.mse > 0.0 ? 10.0 * std::log10(peak * peak / result.mse)
                                 : std::numeric_limits<double>::infinity();
  const double c1 = std::pow(0.01 * peak, 2.0);
  const double c2 = std::pow(0.03 * peak, 2.0);
  result.ssim = ((2.0 * meanReference * meanTest + c1) * (2.0 * covariance + c2)) /
                ((meanReference * meanReference + meanTest * meanTest + c1) *
                 (varianceReference + varianceTest + c2));
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 5 && std::string{argv[1]} == "--convergence") {
      const Metrics early = compare(read(argv[2]), read(argv[3]));
      const Metrics late = compare(read(argv[3]), read(argv[4]));
      std::cout << std::setprecision(10) << "{\"early_mse\":" << early.mse
                << ",\"late_mse\":" << late.mse << ",\"early_ssim\":" << early.ssim
                << ",\"late_ssim\":" << late.ssim << "}\n";
      return std::isfinite(early.mse) && std::isfinite(late.mse) && late.mse < early.mse &&
                     late.ssim > early.ssim
                 ? 0
                 : 3;
    }
    if (argc == 6 && std::string{argv[1]} == "--flicker") {
      const Metrics temporal = compare(read(argv[2]), read(argv[3]));
      const double maximumMse = std::stod(argv[4]);
      const double minimumSsim = std::stod(argv[5]);
      std::cout << std::setprecision(10) << "{\"temporal_mse\":" << temporal.mse
                << ",\"temporal_ssim\":" << temporal.ssim
                << ",\"maximum_error\":" << temporal.maximumError << "}\n";
      return std::isfinite(temporal.mse) && std::isfinite(temporal.ssim) &&
                     temporal.mse <= maximumMse && temporal.ssim >= minimumSsim
                 ? 0
                 : 4;
    }
    if (argc != 3 && argc != 4 && argc != 6) {
      throw std::invalid_argument(
          "usage: vulkax-exr-quality reference.exr test.exr [previous.exr] | "
          "reference.exr test.exr --assert max_mse min_ssim | "
          "--convergence sample1.exr sampleN.exr sampleHigh.exr | "
          "--flicker frameA.exr frameB.exr max_mse min_ssim");
    }
    const Metrics quality = compare(read(argv[1]), read(argv[2]));
    std::cout << std::setprecision(10) << "{\"mse\":" << quality.mse << ",\"psnr\":" << quality.psnr
              << ",\"ssim\":" << quality.ssim << ",\"maximum_error\":" << quality.maximumError;
    if (argc == 4) {
      const Metrics temporal = compare(read(argv[3]), read(argv[2]));
      std::cout << ",\"temporal_mse\":" << temporal.mse << ",\"temporal_ssim\":" << temporal.ssim;
    }
    std::cout << "}\n";
    bool valid = std::isfinite(quality.mse) && std::isfinite(quality.ssim);
    if (argc == 6) {
      if (std::string{argv[3]} != "--assert")
        throw std::invalid_argument("quality thresholds require --assert max_mse min_ssim");
      const double maximumMse = std::stod(argv[4]);
      const double minimumSsim = std::stod(argv[5]);
      valid = valid && quality.mse <= maximumMse && quality.ssim >= minimumSsim;
    }
    return valid ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "vulkax-exr-quality: " << error.what() << '\n';
    return 1;
  }
}