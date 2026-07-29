#pragma once

#include "vulkax/equation/equation.hpp"
#include "vulkax/relativity/schwarzschild_lensing.hpp"
#include "vulkax/research/quality_controller.hpp"
#include "vulkax/sim/particle_system.hpp"
#include "vulkax/sim/simulation_graph.hpp"
#include "vulkax/sim/buoyant_smoke.hpp"
#include "vulkax/editor/vulkan_field_executor.hpp"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <map>
#include <optional>
#include <vector>

namespace vulkax::editor {

class StudioController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList presets READ presets CONSTANT)
  Q_PROPERTY(QVariantList parameters READ parameters NOTIFY parametersChanged)
  Q_PROPERTY(QString selectedPreset READ selectedPreset NOTIFY selectedPresetChanged)
  Q_PROPERTY(QString expression READ expression WRITE setExpression NOTIFY expressionChanged)
  Q_PROPERTY(double timelineSeconds READ timelineSeconds WRITE seek NOTIFY timelineChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
  Q_PROPERTY(QUrl previewUrl READ previewUrl NOTIFY previewChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY diagnosticsChanged)
  Q_PROPERTY(double renderFrameMilliseconds READ renderFrameMilliseconds NOTIFY performanceChanged)
  Q_PROPERTY(double gpuDispatchMilliseconds READ gpuDispatchMilliseconds NOTIFY performanceChanged)
  Q_PROPERTY(QString previewBackend READ previewBackend NOTIFY performanceChanged)
  Q_PROPERTY(double resolutionScale READ resolutionScale NOTIFY performanceChanged)
  Q_PROPERTY(double visualError READ visualError NOTIFY performanceChanged)
  Q_PROPERTY(bool visualErrorAvailable READ visualErrorAvailable NOTIFY performanceChanged)
  Q_PROPERTY(QString errorMetric READ errorMetric NOTIFY performanceChanged)

 public:
  explicit StudioController(QObject* parent = nullptr);

  [[nodiscard]] QVariantList presets() const;
  [[nodiscard]] QVariantList parameters() const;
  [[nodiscard]] QString selectedPreset() const;
  [[nodiscard]] QString expression() const;
  [[nodiscard]] double timelineSeconds() const;
  [[nodiscard]] bool playing() const;
  [[nodiscard]] QUrl previewUrl() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString diagnostics() const;
  [[nodiscard]] double renderFrameMilliseconds() const;
  [[nodiscard]] double gpuDispatchMilliseconds() const;
  [[nodiscard]] QString previewBackend() const;
  [[nodiscard]] double resolutionScale() const;
  [[nodiscard]] double visualError() const;
  [[nodiscard]] bool visualErrorAvailable() const;
  [[nodiscard]] QString errorMetric() const;
  [[nodiscard]] QImage previewImage() const;

  Q_INVOKABLE void selectPreset(const QString& id);
  Q_INVOKABLE void setExpression(const QString& value);
  Q_INVOKABLE bool compileExpression();
  Q_INVOKABLE void setParameter(const QString& name, double value);
  Q_INVOKABLE void togglePlayback();
  Q_INVOKABLE void seek(double seconds);
  // Receives the physical viewport extent from QML. The CPU fallback uses it
  // immediately; a later direct GPU viewport consumes the same contract.
  Q_INVOKABLE void setPreviewExtent(double logicalWidth, double logicalHeight, double devicePixelRatio);
  Q_INVOKABLE void advance();
  Q_INVOKABLE void saveProjectDialog();
  Q_INVOKABLE void openProjectDialog();
  Q_INVOKABLE bool saveProject(const QString& filePath);
  Q_INVOKABLE bool openProject(const QString& filePath);
  Q_INVOKABLE bool exportPng(const QString& filePath);
  Q_INVOKABLE void exportPngDialog();
  Q_INVOKABLE bool exportExr(const QString& filePath);
  Q_INVOKABLE void exportExrDialog();
  Q_INVOKABLE bool exportSequence(const QString& directory, int frameCount = 120, double framesPerSecond = 30.0);
  Q_INVOKABLE void exportSequenceDialog();

 signals:
  void parametersChanged();
  void selectedPresetChanged();
  void expressionChanged();
  void timelineChanged();
  void playingChanged();
  void previewChanged();
  void statusChanged();
  void diagnosticsChanged();
  void performanceChanged();

 private:
  void selectPreset(const equation::EquationPreset& preset);
  void rebuildDynamicSimulation();
  void renderPreview();
  void setStatus(QString value);

  std::vector<equation::EquationPreset> presets_;
  equation::EquationPreset activePreset_;
  std::optional<equation::ScalarExpression> compiledExpression_;
  std::map<std::string, double> parameterValues_;
  QString expression_;
  QString diagnostics_;
  QString status_;
  double timelineSeconds_ = 0.0;
  int previewTargetWidth_ = 1280;
  int previewTargetHeight_ = 720;
  bool playing_ = true;
  quint64 previewRevision_ = 0;
  mutable QMutex previewMutex_;
  QImage preview_;
  std::optional<sim::SimulationGraph> simulation_;
  std::optional<sim::BuoyantSmokeSimulation> smokeSimulation_;
  bool gpuReactionActive_ = false;
  std::vector<float> gpuReactionPrimary_;
  std::vector<float> gpuReactionSecondary_;
  double reactionGpuDispatchMilliseconds_ = -1.0;
  std::optional<sim::ParticleGravitySystem> particleSystem_;
  std::optional<relativity::SchwarzschildThinDiskRenderer> lensingRenderer_;
  research::QualityController qualityController_{{16.67, 0.005, 0.35, 1.0, 1, 8}};
  VulkanFieldExecutor gpuFieldExecutor_;
  double renderFrameMilliseconds_ = 0.0;
  double gpuDispatchMilliseconds_ = -1.0;
  QString previewBackend_ = "CPU analytical reference";
  std::optional<double> visualError_;
  bool freezeQuality_ = false;
};

}  // namespace vulkax::editor
