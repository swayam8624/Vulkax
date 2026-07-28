#include "vulkax/editor/studio_controller.hpp"

#include "vulkax/equation/glsl_compiler.hpp"

#include <QFileDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#if defined(VULKAX_HAS_OPENEXR)
#include <OpenEXR/ImfRgbaFile.h>
#endif

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace vulkax::editor {
namespace {

constexpr int kPreviewWidth = 640;
constexpr int kPreviewHeight = 360;
constexpr int kErrorReferenceWidth = 80;
constexpr int kErrorReferenceHeight = 45;

void paintParticle(QImage& image, int centerX, int centerY, int radius, QRgb color) {
  for (int y = std::max(0, centerY - radius); y <= std::min(image.height() - 1, centerY + radius); ++y) {
    auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = std::max(0, centerX - radius); x <= std::min(image.width() - 1, centerX + radius); ++x) {
      const int deltaX = x - centerX;
      const int deltaY = y - centerY;
      if (deltaX * deltaX + deltaY * deltaY <= radius * radius) pixels[x] = color;
    }
  }
}

QRgb transfer(float normalized) {
  normalized = std::clamp(normalized, 0.0f, 1.0f);
  // A restrained indigo-to-amber scientific palette preserves sign/detail
  // without the diagnostic rainbow look of the earlier preview.
  const float shadow = std::pow(normalized, 0.72f);
  const float red = 0.025f + 0.91f * std::pow(shadow, 1.55f);
  const float green = 0.055f + 0.54f * std::sin(shadow * 1.47f);
  const float blue = 0.13f + 0.70f * (1.0f - shadow) * (1.0f - shadow);
  return qRgba(static_cast<int>(std::clamp(red, 0.0f, 1.0f) * 255.0f),
               static_cast<int>(std::clamp(green, 0.0f, 1.0f) * 255.0f),
               static_cast<int>(std::clamp(blue, 0.0f, 1.0f) * 255.0f), 255);
}

double fract(double value) { return value - std::floor(value); }

float srgbToLinear(float value) {
  return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float sampleBilinear(
    const std::vector<float>& field, uint32_t fieldWidth, uint32_t fieldHeight, float x, float y) {
  if (field.empty() || fieldWidth == 0 || fieldHeight == 0) return 0.0f;
  x = std::clamp(x, 0.0f, static_cast<float>(fieldWidth - 1));
  y = std::clamp(y, 0.0f, static_cast<float>(fieldHeight - 1));
  const uint32_t x0 = static_cast<uint32_t>(x);
  const uint32_t y0 = static_cast<uint32_t>(y);
  const uint32_t x1 = std::min(fieldWidth - 1, x0 + 1);
  const uint32_t y1 = std::min(fieldHeight - 1, y0 + 1);
  const float tx = x - static_cast<float>(x0);
  const float ty = y - static_cast<float>(y0);
  const auto value = [&](uint32_t sampleX, uint32_t sampleY) {
    return field[static_cast<size_t>(sampleY) * fieldWidth + sampleX];
  };
  const float lower = value(x0, y0) * (1.0f - tx) + value(x1, y0) * tx;
  const float upper = value(x0, y1) * (1.0f - tx) + value(x1, y1) * tx;
  return lower * (1.0f - ty) + upper * ty;
}

double starField(double x, double y) {
  const double cellX = std::floor(x * 13.0);
  const double cellY = std::floor(y * 13.0);
  const double noise = fract(std::sin(cellX * 127.1 + cellY * 311.7) * 43758.5453123);
  const double localX = fract(x * 13.0) - 0.5;
  const double localY = fract(y * 13.0) - 0.5;
  const double point = std::exp(-(localX * localX + localY * localY) / 0.0035);
  return noise > 0.89 ? point * (0.45 + noise * 0.55) : 0.0;
}

}  // namespace

StudioController::StudioController(QObject* parent) : QObject(parent) {
  presets_ = equation::builtInPresets();
  selectPreset(presets_.front());
}

QVariantList StudioController::presets() const {
  QVariantList result;
  for (const auto& preset : presets_) {
    QVariantMap value;
    value.insert("id", QString::fromStdString(preset.id));
    value.insert("name", QString::fromStdString(preset.displayName));
    value.insert("description", QString::fromStdString(preset.description));
    result.push_back(value);
  }
  return result;
}

QVariantList StudioController::parameters() const {
  QVariantList result;
  for (const auto& parameter : activePreset_.parameters) {
    QVariantMap value;
    value.insert("name", QString::fromStdString(parameter.name));
    value.insert("units", QString::fromStdString(parameter.units));
    value.insert("minimum", parameter.minimum);
    value.insert("maximum", parameter.maximum);
    const auto found = parameterValues_.find(parameter.name);
    value.insert("value", found == parameterValues_.end() ? parameter.value : found->second);
    result.push_back(value);
  }
  return result;
}

QString StudioController::selectedPreset() const { return QString::fromStdString(activePreset_.id); }
QString StudioController::expression() const { return expression_; }
double StudioController::timelineSeconds() const { return timelineSeconds_; }
bool StudioController::playing() const { return playing_; }
QUrl StudioController::previewUrl() const {
  return QUrl("image://vulkax-field/" + QString::number(previewRevision_));
}
QString StudioController::status() const { return status_; }
QString StudioController::diagnostics() const { return diagnostics_; }
double StudioController::renderFrameMilliseconds() const { return renderFrameMilliseconds_; }
double StudioController::gpuDispatchMilliseconds() const { return gpuDispatchMilliseconds_; }
QString StudioController::previewBackend() const { return previewBackend_; }
double StudioController::resolutionScale() const { return qualityController_.state().resolutionScale; }
double StudioController::visualError() const { return visualError_; }
QString StudioController::errorMetric() const {
  if (simulation_ && gpuReactionActive_) return "GPU/CPU solver MSE";
  if (simulation_ || smokeSimulation_ || particleSystem_ || lensingRenderer_) return "reference unavailable";
  return "sampling MSE";
}

QImage StudioController::previewImage() const {
  QMutexLocker lock(&previewMutex_);
  return preview_;
}

void StudioController::selectPreset(const QString& id) {
  const auto found = std::find_if(presets_.begin(), presets_.end(), [&](const auto& preset) {
    return QString::fromStdString(preset.id) == id;
  });
  if (found != presets_.end()) selectPreset(*found);
}

void StudioController::selectPreset(const equation::EquationPreset& preset) {
  activePreset_ = preset;
  timelineSeconds_ = 0.0;
  simulation_.reset();
  smokeSimulation_.reset();
  gpuReactionActive_ = false;
  gpuReactionPrimary_.clear();
  gpuReactionSecondary_.clear();
  reactionGpuDispatchMilliseconds_ = -1.0;
  particleSystem_.reset();
  lensingRenderer_.reset();
  parameterValues_.clear();
  // Do not carry a previous scalar-field downscale into a simulation that has
  // no independently measured visual error.
  qualityController_.reset({1.0, 1, 1});
  for (const auto& parameter : activePreset_.parameters) parameterValues_[parameter.name] = parameter.value;
  if (preset.id == "schwarzschild-lensing") {
    lensingRenderer_.emplace();
  } else {
    rebuildDynamicSimulation();
  }
  expression_ = QString::fromStdString(activePreset_.expressions.front());
  compiledExpression_.reset();
  emit selectedPresetChanged();
  emit parametersChanged();
  emit expressionChanged();
  emit timelineChanged();
  compileExpression();
}

void StudioController::rebuildDynamicSimulation() {
  gpuReactionActive_ = false;
  gpuReactionPrimary_.clear();
  gpuReactionSecondary_.clear();
  reactionGpuDispatchMilliseconds_ = -1.0;
  const auto parameter = [&](const std::string& name, double fallback) {
    const auto found = parameterValues_.find(name);
    return found == parameterValues_.end() ? fallback : found->second;
  };
  if (activePreset_.id == "reaction-diffusion-seed") {
    sim::SimulationConfig config{};
    config.kind = sim::SimulationKind::ReactionDiffusion;
    config.width = 128;
    config.height = 128;
    config.timestepSeconds = 1.0f / 60.0f;
    config.spatialStep = 1.0f;
    config.seed = 1337;
    config.diffusionA = static_cast<float>(parameter("diffusion_a", 1.0));
    config.diffusionB = static_cast<float>(parameter("diffusion_b", 0.5));
    config.feed = static_cast<float>(parameter("feed", 0.0367));
    config.kill = static_cast<float>(parameter("kill", 0.0649));
    simulation_.emplace(config);
    const uint32_t steps = static_cast<uint32_t>(std::llround(timelineSeconds_ * 60.0));
    if (gpuFieldExecutor_.available()) {
      try {
        gpuFieldExecutor_.resetReaction(
            {config.width, config.height, config.timestepSeconds, config.diffusionA, config.diffusionB,
             config.feed, config.kill},
            simulation_->primaryField(), simulation_->secondaryField());
        const auto result = gpuFieldExecutor_.stepReaction(steps);
        gpuReactionPrimary_ = result.primary;
        gpuReactionSecondary_ = result.secondary;
        reactionGpuDispatchMilliseconds_ = result.dispatchMilliseconds;
        gpuReactionActive_ = true;
      } catch (const std::exception&) {
        gpuReactionActive_ = false;
      }
    }
    // Keep the deterministic CPU graph as an independent live reference for
    // the GPU state; it is not the displayed reaction-diffusion field.
    simulation_->step(steps);
  } else if (activePreset_.id == "buoyant-smoke") {
    sim::BuoyantSmokeConfig config{};
    config.width = 128;
    config.height = 128;
    config.buoyancy = static_cast<float>(parameter("buoyancy", config.buoyancy));
    config.vorticityConfinement = static_cast<float>(parameter("vorticity", config.vorticityConfinement));
    config.densityDissipation = static_cast<float>(
        parameter("density_dissipation", config.densityDissipation));
    smokeSimulation_.emplace(config);
    smokeSimulation_->step(static_cast<uint32_t>(std::llround(timelineSeconds_ * 60.0)));
  } else if (activePreset_.id == "nbody-orbits") {
    sim::ParticleGravityConfig config{};
    config.centralMass = parameter("central_mass", config.centralMass);
    config.orbiterMass = parameter("orbiter_mass", config.orbiterMass);
    config.softening = parameter("softening", config.softening);
    particleSystem_.emplace(config);
    particleSystem_->step(static_cast<uint32_t>(std::llround(timelineSeconds_ * 240.0)));
  }
}

void StudioController::setExpression(const QString& value) {
  if (expression_ == value) return;
  expression_ = value;
  emit expressionChanged();
}

bool StudioController::compileExpression() {
  try {
    activePreset_.expressions = {expression_.toStdString()};
    compiledExpression_.emplace(equation::parseScalarExpression(activePreset_.expressions.front()));
    const auto symbols = equation::variableNames(*compiledExpression_);
    const std::set<std::string> coordinates{"x", "y", "z", "t"};
    std::map<std::string, equation::Parameter> existing;
    for (const auto& parameter : activePreset_.parameters) existing.emplace(parameter.name, parameter);
    std::vector<equation::Parameter> derived;
    std::map<std::string, double> nextValues;
    for (const std::string& symbol : symbols) {
      if (coordinates.contains(symbol)) continue;
      const auto known = existing.find(symbol);
      const double value = parameterValues_.contains(symbol) ? parameterValues_.at(symbol) :
          (known == existing.end() ? 1.0 : known->second.value);
      if (known != existing.end()) {
        derived.push_back(known->second);
      } else {
        derived.push_back({symbol, value, "", -10.0, 10.0});
      }
      nextValues.emplace(symbol, value);
    }
    activePreset_.parameters = std::move(derived);
    parameterValues_ = std::move(nextValues);
    emit parametersChanged();
    const auto glsl = equation::compilePresetToGlsl(activePreset_);
    diagnostics_ = glsl.succeeded
        ? QString("GLSL compute contract ready: %1 bytes").arg(glsl.computeShader.size())
        : QString::fromStdString(glsl.diagnostics.empty() ? "GLSL emission failed" : glsl.diagnostics.front());
    emit diagnosticsChanged();
    renderPreview();
    setStatus(QString("Live preview compiled; %1 adjustable parameter%2 extracted from the equation")
                  .arg(activePreset_.parameters.size())
                  .arg(activePreset_.parameters.size() == 1 ? "" : "s"));
    return true;
  } catch (const std::exception& error) {
    diagnostics_ = QString::fromUtf8(error.what());
    emit diagnosticsChanged();
    setStatus("Equation has an error; the last valid preview remains active");
    return false;
  }
}

void StudioController::setParameter(const QString& name, double value) {
  const auto found = std::find_if(activePreset_.parameters.begin(), activePreset_.parameters.end(), [&](const auto& parameter) {
    return QString::fromStdString(parameter.name) == name;
  });
  if (found == activePreset_.parameters.end()) return;
  parameterValues_[found->name] = std::clamp(value, found->minimum, found->maximum);
  rebuildDynamicSimulation();
  emit parametersChanged();
  renderPreview();
}

void StudioController::togglePlayback() {
  playing_ = !playing_;
  emit playingChanged();
}

void StudioController::seek(double seconds) {
  timelineSeconds_ = std::max(0.0, seconds);
  rebuildDynamicSimulation();
  emit timelineChanged();
  renderPreview();
}

void StudioController::setPreviewExtent(
    double logicalWidth, double logicalHeight, double devicePixelRatio) {
  if (!std::isfinite(logicalWidth) || !std::isfinite(logicalHeight) || !std::isfinite(devicePixelRatio) ||
      logicalWidth < 32.0 || logicalHeight < 32.0 || devicePixelRatio <= 0.0) {
    return;
  }
  const int width = std::max(64, static_cast<int>(std::lround(logicalWidth * devicePixelRatio)));
  const int height = std::max(64, static_cast<int>(std::lround(logicalHeight * devicePixelRatio)));
  if (width == previewTargetWidth_ && height == previewTargetHeight_) return;
  previewTargetWidth_ = width;
  previewTargetHeight_ = height;
  renderPreview();
}

void StudioController::advance() {
  if (!playing_) return;
  timelineSeconds_ += 1.0 / 60.0;
  if (simulation_) {
    if (gpuReactionActive_) {
      try {
        const auto result = gpuFieldExecutor_.stepReaction(1);
        gpuReactionPrimary_ = result.primary;
        gpuReactionSecondary_ = result.secondary;
        reactionGpuDispatchMilliseconds_ = result.dispatchMilliseconds;
      } catch (const std::exception&) {
        gpuReactionActive_ = false;
        gpuReactionPrimary_.clear();
        gpuReactionSecondary_.clear();
        reactionGpuDispatchMilliseconds_ = -1.0;
      }
    }
    simulation_->step();
  }
  if (smokeSimulation_) smokeSimulation_->step();
  if (particleSystem_) particleSystem_->step(4);
  emit timelineChanged();
  renderPreview();
}

void StudioController::renderPreview() {
  if (!compiledExpression_) return;
  QElapsedTimer timer;
  timer.start();
  gpuDispatchMilliseconds_ = gpuReactionActive_ ? reactionGpuDispatchMilliseconds_ : -1.0;
  const bool hasMeasuredVisualError = simulation_ && gpuReactionActive_;
  const double resolutionScale = hasMeasuredVisualError ? qualityController_.state().resolutionScale : 1.0;
  const int width = std::max(64, static_cast<int>(std::lround(previewTargetWidth_ * resolutionScale)));
  const int height = std::max(64, static_cast<int>(std::lround(previewTargetHeight_ * resolutionScale)));
  // QRgb scanline writes use Qt's native ARGB word representation. Using the
  // matching image format avoids byte-swapping colors on little-endian hosts.
  QImage image{width, height, QImage::Format_ARGB32};
  if (lensingRenderer_) {
    previewBackend_ = "CPU Schwarzschild thin-disk null-geodesic lensing";
    for (int row = 0; row < height; ++row) {
      auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(row));
      const double screenY = (static_cast<double>(row) / (height - 1) - 0.5) * 2.0;
      for (int column = 0; column < width; ++column) {
        const double screenX = (static_cast<double>(column) / (width - 1) - 0.5) * 2.0 *
            static_cast<double>(width) / static_cast<double>(height);
        const double radius = std::sqrt(screenX * screenX + screenY * screenY);
        const auto sample = lensingRenderer_->sample(screenX, screenY, timelineSeconds_ * 0.10);
        if (sample.captured) {
          pixels[column] = qRgba(0, 0, 0, 255);
          continue;
        }
        const double angle = std::atan2(screenY, screenX) + sample.deflectionRadians;
        const double sourceRadius = radius + 0.045 * std::sin(sample.deflectionRadians);
        const double star = starField(sourceRadius * std::cos(angle) + 7.0, sourceRadius * std::sin(angle) + 11.0);
        const int red = static_cast<int>(std::clamp(4.0 + 255.0 * sample.red + 145.0 * star, 0.0, 255.0));
        const int green = static_cast<int>(std::clamp(6.0 + 255.0 * sample.green + 170.0 * star, 0.0, 255.0));
        const int blue = static_cast<int>(std::clamp(13.0 + 255.0 * sample.blue + 245.0 * star, 0.0, 255.0));
        pixels[column] = qRgba(red, green, blue, 255);
      }
    }
    renderFrameMilliseconds_ = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    visualError_ = 0.0;  // No independent exact image reference for lensing preview.
    // Lensing has no independent image reference. It must never report an
    // invented zero error to the quality controller.
    emit performanceChanged();
    {
      QMutexLocker lock(&previewMutex_);
      preview_ = std::move(image);
      ++previewRevision_;
    }
    emit previewChanged();
    return;
  }
  if (particleSystem_) {
    previewBackend_ = "CPU N-body velocity Verlet";
    image.fill(qRgba(8, 12, 17, 255));
    constexpr double viewHalfExtent = 11.0;
    const auto& particles = particleSystem_->particles();
    for (size_t index = 0; index < particles.size(); ++index) {
      const auto& particle = particles[index];
      const int x = static_cast<int>(std::lround((particle.position.x / (2.0 * viewHalfExtent) + 0.5) * (width - 1)));
      const int y = static_cast<int>(std::lround((0.5 - particle.position.y / (2.0 * viewHalfExtent)) * (height - 1)));
      paintParticle(image, x, y, index == 0 ? 10 : 4, index == 0 ? qRgba(255, 206, 96, 255) : qRgba(112, 210, 255, 255));
    }
    renderFrameMilliseconds_ = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    visualError_ = 0.0;  // Particle raster output is not yet compared against a separate image reference.
    // Particle output has no independent image reference.
    emit performanceChanged();
    {
      QMutexLocker lock(&previewMutex_);
      preview_ = std::move(image);
      ++previewRevision_;
    }
    emit previewChanged();
    return;
  }
  if (smokeSimulation_) {
    previewBackend_ = "CPU incompressible buoyant smoke";
    image.fill(qRgba(5, 8, 12, 255));
    const auto& config = smokeSimulation_->config();
    const auto& density = smokeSimulation_->density();
    const auto& temperature = smokeSimulation_->temperature();
    for (int row = 0; row < height; ++row) {
      auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(row));
      const float fieldY = height <= 1 ? 0.0f :
          static_cast<float>(row) * static_cast<float>(config.height - 1) / static_cast<float>(height - 1);
      for (int column = 0; column < width; ++column) {
        const float fieldX = width <= 1 ? 0.0f :
            static_cast<float>(column) * static_cast<float>(config.width - 1) / static_cast<float>(width - 1);
        const float soot = std::clamp(sampleBilinear(density, config.width, config.height, fieldX, fieldY), 0.0f, 1.0f);
        const float heat = std::clamp(sampleBilinear(temperature, config.width, config.height, fieldX, fieldY), 0.0f, 1.0f);
        const float glow = std::pow(heat, 1.35f);
        const float haze = std::pow(soot, 0.70f);
        const float opacity = 0.78f * (1.0f - std::exp(-1.6f * haze));
        const float smokeRed = 52.0f * haze + 235.0f * glow;
        const float smokeGreen = 72.0f * haze + 104.0f * glow;
        const float smokeBlue = 106.0f * haze + 28.0f * glow;
        const int red = static_cast<int>(std::clamp(4.0f * (1.0f - opacity) + smokeRed * opacity, 0.0f, 255.0f));
        const int green = static_cast<int>(std::clamp(7.0f * (1.0f - opacity) + smokeGreen * opacity, 0.0f, 255.0f));
        const int blue = static_cast<int>(std::clamp(16.0f * (1.0f - opacity) + smokeBlue * opacity, 0.0f, 255.0f));
        pixels[column] = qRgba(red, green, blue, 255);
      }
    }
    renderFrameMilliseconds_ = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    visualError_ = 0.0;
    // Smoke has no independently measured image error. Keep full viewport
    // resolution rather than treating unavailable error as zero.
    emit performanceChanged();
    {
      QMutexLocker lock(&previewMutex_);
      preview_ = std::move(image);
      ++previewRevision_;
    }
    emit previewChanged();
    return;
  }
  std::unordered_map<std::string, double> variables;
  variables.reserve(parameterValues_.size() + 4);
  for (const auto& [name, value] : parameterValues_) variables[name] = value;
  variables["t"] = timelineSeconds_;
  variables["z"] = 0.0;
  std::vector<float> gpuValues;
  bool usingGpuWave = activePreset_.id == "wave-field" && gpuFieldExecutor_.available();
  if (usingGpuWave) {
    const auto parameter = [&](const std::string& name, double fallback) {
      const auto found = parameterValues_.find(name);
      return found == parameterValues_.end() ? fallback : found->second;
    };
    try {
      auto gpuResult = gpuFieldExecutor_.evaluateWave({
          static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<float>(timelineSeconds_),
          static_cast<float>(parameter("amplitude", 1.0)), static_cast<float>(parameter("wavenumber", 2.0)),
          static_cast<float>(parameter("angular_frequency", 3.0))});
      if (!gpuResult.hdrFrameProduced) {
        throw std::runtime_error("Vulkan wave dispatch did not produce its RGBA16F frame");
      }
      gpuDispatchMilliseconds_ = gpuResult.dispatchMilliseconds;
      gpuValues = std::move(gpuResult.values);
      previewBackend_ = QString("Persistent Vulkan compute + RGBA16F frame: %1")
          .arg(QString::fromStdString(gpuFieldExecutor_.deviceName()));
    } catch (const std::exception&) {
      usingGpuWave = false;
      previewBackend_ = QString::fromStdString(gpuFieldExecutor_.diagnostic());
    }
  }
  if (simulation_ && gpuReactionActive_) {
    previewBackend_ = QString("Persistent Vulkan Gray-Scott compute: %1")
        .arg(QString::fromStdString(gpuFieldExecutor_.deviceName()));
  } else if (!usingGpuWave) {
    previewBackend_ = "CPU analytical reference";
  }
  std::vector<double> values(static_cast<size_t>(width) * height);
  for (int row = 0; row < height; ++row) {
    auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(row));
    const double y = (1.0 - static_cast<double>(row) / (height - 1)) * 8.0 - 4.0;
    variables["y"] = y;
    for (int column = 0; column < width; ++column) {
      variables["x"] = (static_cast<double>(column) / (width - 1)) * 8.0 - 4.0;
      double value = usingGpuWave ? gpuValues[static_cast<size_t>(row) * width + column]
                                  : compiledExpression_->evaluate(variables);
      if (simulation_) {
        const float fieldX = width <= 1 ? 0.0f : static_cast<float>(column) *
            static_cast<float>(simulation_->config().width - 1) / static_cast<float>(width - 1);
        const float fieldY = height <= 1 ? 0.0f : static_cast<float>(row) *
            static_cast<float>(simulation_->config().height - 1) / static_cast<float>(height - 1);
        const auto& field = gpuReactionActive_ ? gpuReactionSecondary_ : simulation_->secondaryField();
        value = sampleBilinear(field, simulation_->config().width, simulation_->config().height, fieldX, fieldY);
      }
      values[static_cast<size_t>(row) * width + column] = value;
      const float mapped = static_cast<float>(0.5 + 0.5 * std::tanh(value));
      pixels[column] = transfer(mapped);
    }
  }
  renderFrameMilliseconds_ = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
  if (simulation_) {
    if (gpuReactionActive_ && gpuReactionSecondary_.size() == simulation_->secondaryField().size()) {
      double squaredError = 0.0;
      for (size_t index = 0; index < gpuReactionSecondary_.size(); ++index) {
        const double difference = static_cast<double>(gpuReactionSecondary_[index]) -
            static_cast<double>(simulation_->secondaryField()[index]);
        squaredError += difference * difference;
      }
      visualError_ = squaredError / static_cast<double>(gpuReactionSecondary_.size());
    } else {
      visualError_ = 0.0;  // A CPU PDE output needs an independent simulation reference.
    }
  } else {
    // Compare the selected preview grid to an independent fixed analytical
    // reference grid. This is a sampling MSE, not a perceptual image metric.
    double squaredError = 0.0;
    std::unordered_map<std::string, double> referenceVariables;
    referenceVariables.reserve(parameterValues_.size() + 4);
    for (const auto& [name, value] : parameterValues_) referenceVariables[name] = value;
    referenceVariables["t"] = timelineSeconds_;
    referenceVariables["z"] = 0.0;
    for (int row = 0; row < kErrorReferenceHeight; ++row) {
      referenceVariables["y"] = (1.0 - static_cast<double>(row) / (kErrorReferenceHeight - 1)) * 8.0 - 4.0;
      const int sampledRow = std::min(height - 1, static_cast<int>(
          (static_cast<int64_t>(row) * height) / kErrorReferenceHeight));
      for (int column = 0; column < kErrorReferenceWidth; ++column) {
        referenceVariables["x"] = (static_cast<double>(column) / (kErrorReferenceWidth - 1)) * 8.0 - 4.0;
        const int sampledColumn = std::min(width - 1, static_cast<int>(
            (static_cast<int64_t>(column) * width) / kErrorReferenceWidth));
        const double difference = values[static_cast<size_t>(sampledRow) * width + sampledColumn] -
            compiledExpression_->evaluate(referenceVariables);
        squaredError += difference * difference;
      }
    }
    visualError_ = squaredError / static_cast<double>(kErrorReferenceWidth * kErrorReferenceHeight);
  }
  if (!freezeQuality_ && hasMeasuredVisualError) {
    qualityController_.update({renderFrameMilliseconds_, 0.0, visualError_});
  }
  emit performanceChanged();
  {
    QMutexLocker lock(&previewMutex_);
    preview_ = std::move(image);
    ++previewRevision_;
  }
  emit previewChanged();
}

void StudioController::saveProjectDialog() {
  const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Untitled.vxp";
  const QString path = QFileDialog::getSaveFileName(nullptr, "Save Vulkax project", defaultPath, "Vulkax Project (*.vxp)");
  if (!path.isEmpty()) saveProject(path);
}

void StudioController::openProjectDialog() {
  const QString path = QFileDialog::getOpenFileName(nullptr, "Open Vulkax project", {}, "Vulkax Project (*.vxp)");
  if (!path.isEmpty()) openProject(path);
}

bool StudioController::saveProject(const QString& filePath) {
  QJsonObject parameters;
  for (const auto& [name, value] : parameterValues_) parameters.insert(QString::fromStdString(name), value);
  QJsonObject document{
      {"format", "vulkax.physics-project"}, {"version", 1},
      {"preset", selectedPreset()}, {"expression", expression_},
      {"timeline_seconds", timelineSeconds_}, {"parameters", parameters},
  };
  QFile file{filePath};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    setStatus("Could not save project");
    return false;
  }
  file.write(QJsonDocument{document}.toJson(QJsonDocument::Indented));
  setStatus("Saved project: " + QFileInfo(file).fileName());
  return true;
}

bool StudioController::openProject(const QString& filePath) {
  QFile file{filePath};
  if (!file.open(QIODevice::ReadOnly)) {
    setStatus("Could not open project");
    return false;
  }
  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject() ||
      document.object().value("format") != "vulkax.physics-project") {
    setStatus("Invalid Vulkax project");
    return false;
  }
  const QJsonObject object = document.object();
  selectPreset(object.value("preset").toString());
  setExpression(object.value("expression").toString(expression_));
  const QJsonObject parameters = object.value("parameters").toObject();
  for (auto iterator = parameters.begin(); iterator != parameters.end(); ++iterator) {
    setParameter(iterator.key(), iterator.value().toDouble());
  }
  seek(object.value("timeline_seconds").toDouble());
  const bool compiled = compileExpression();
  if (compiled) setStatus("Opened project: " + QFileInfo(file).fileName());
  return compiled;
}

bool StudioController::exportPng(const QString& filePath) {
  const QImage image = previewImage();
  if (image.isNull() || !image.save(filePath, "PNG")) {
    setStatus("Could not export PNG");
    return false;
  }
  setStatus("Exported PNG: " + QFileInfo(filePath).fileName());
  return true;
}

void StudioController::exportPngDialog() {
  const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/VulkaxFrame.png";
  const QString path = QFileDialog::getSaveFileName(nullptr, "Export Vulkax frame", defaultPath, "PNG Image (*.png)");
  if (!path.isEmpty()) exportPng(path);
}

bool StudioController::exportExr(const QString& filePath) {
#if defined(VULKAX_HAS_OPENEXR)
  const QImage image = previewImage().convertToFormat(QImage::Format_ARGB32);
  if (image.isNull()) {
    setStatus("Could not export EXR: no preview frame");
    return false;
  }
  try {
    std::vector<OPENEXR_IMF_NAMESPACE::Rgba> pixels(
        static_cast<size_t>(image.width()) * static_cast<size_t>(image.height()));
    for (int row = 0; row < image.height(); ++row) {
      const auto* source = reinterpret_cast<const QRgb*>(image.constScanLine(row));
      for (int column = 0; column < image.width(); ++column) {
        const QRgb color = source[column];
        auto& destination = pixels[static_cast<size_t>(row) * image.width() + column];
        destination.r = srgbToLinear(static_cast<float>(qRed(color)) / 255.0f);
        destination.g = srgbToLinear(static_cast<float>(qGreen(color)) / 255.0f);
        destination.b = srgbToLinear(static_cast<float>(qBlue(color)) / 255.0f);
        destination.a = static_cast<float>(qAlpha(color)) / 255.0f;
      }
    }
    OPENEXR_IMF_NAMESPACE::RgbaOutputFile output(
        filePath.toStdString().c_str(), image.width(), image.height(), OPENEXR_IMF_NAMESPACE::WRITE_RGBA);
    output.setFrameBuffer(pixels.data(), 1, image.width());
    output.writePixels(image.height());
  } catch (const std::exception& error) {
    setStatus("Could not export EXR: " + QString::fromUtf8(error.what()));
    return false;
  }
  setStatus("Exported linear EXR preview: " + QFileInfo(filePath).fileName());
  return true;
#else
  Q_UNUSED(filePath);
  setStatus("EXR export unavailable: OpenEXR was not found at build time");
  return false;
#endif
}

void StudioController::exportExrDialog() {
  const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/VulkaxFrame.exr";
  const QString path = QFileDialog::getSaveFileName(nullptr, "Export linear Vulkax preview", defaultPath, "OpenEXR Image (*.exr)");
  if (!path.isEmpty()) exportExr(path);
}

bool StudioController::exportSequence(
    const QString& directory,
    int frameCount,
    double framesPerSecond) {
  if (directory.isEmpty() || frameCount <= 0 || framesPerSecond <= 0.0 || !compiledExpression_) {
    setStatus("Invalid sequence export settings");
    return false;
  }
  QDir output{directory};
  if (!output.exists() && !output.mkpath(".")) {
    setStatus("Could not create export directory");
    return false;
  }
  const double originalTime = timelineSeconds_;
  // Offline exports use a stable reference resolution. Interactive timing
  // adaptation resumes after the job instead of leaking into the frame set.
  qualityController_.reset({1.0, 1, 1});
  freezeQuality_ = true;
  QJsonArray files;
  for (int frame = 0; frame < frameCount; ++frame) {
    timelineSeconds_ = originalTime + static_cast<double>(frame) / framesPerSecond;
    rebuildDynamicSimulation();
    renderPreview();
    const QString filename = QString("frame_%1.png").arg(frame, 5, 10, QLatin1Char('0'));
    if (!previewImage().save(output.filePath(filename), "PNG")) {
      timelineSeconds_ = originalTime;
      rebuildDynamicSimulation();
      freezeQuality_ = false;
      renderPreview();
      setStatus("Sequence export failed at frame " + QString::number(frame));
      return false;
    }
    files.append(filename);
  }
  QJsonObject manifest{
      {"format", "vulkax.physics-sequence"}, {"version", 1},
      {"expression", expression_}, {"preset", selectedPreset()},
      {"frames", frameCount}, {"frames_per_second", framesPerSecond},
      {"start_time_seconds", originalTime}, {"width", previewImage().width()}, {"height", previewImage().height()},
      {"files", files},
  };
  QFile manifestFile{output.filePath("sequence_manifest.json")};
  if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    timelineSeconds_ = originalTime;
    rebuildDynamicSimulation();
    freezeQuality_ = false;
    renderPreview();
    setStatus("Could not write sequence manifest");
    return false;
  }
  manifestFile.write(QJsonDocument{manifest}.toJson(QJsonDocument::Indented));
  timelineSeconds_ = originalTime;
  rebuildDynamicSimulation();
  freezeQuality_ = false;
  emit timelineChanged();
  renderPreview();
  setStatus(QString("Exported %1-frame PNG sequence").arg(frameCount));
  return true;
}

void StudioController::exportSequenceDialog() {
  const QString directory = QFileDialog::getExistingDirectory(
      nullptr, "Choose Vulkax sequence folder",
      QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
  if (!directory.isEmpty()) exportSequence(directory);
}

void StudioController::setStatus(QString value) {
  if (status_ == value) return;
  status_ = std::move(value);
  emit statusChanged();
}

}  // namespace vulkax::editor
