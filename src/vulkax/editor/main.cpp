#include "vulkax/editor/studio_controller.hpp"

#include <QApplication>
#include <QDebug>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

class FieldImageProvider final : public QQuickImageProvider {
 public:
  explicit FieldImageProvider(const vulkax::editor::StudioController& controller)
      : QQuickImageProvider(QQuickImageProvider::Image), controller_(controller) {}

  QImage requestImage(const QString&, QSize* size, const QSize&) override {
    const QImage image = controller_.previewImage();
    if (size) *size = image.size();
    return image;
  }

 private:
  const vulkax::editor::StudioController& controller_;
};

}  // namespace

int main(int argc, char* argv[]) {
  // Qt 6.11's macOS native-controls plugin renders through SwiftUI and is
  // unstable on this machine when Qt Quick is backed by Vulkan. Basic controls
  // are fully QML-rendered and keep the editor independent of that bridge.
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QApplication application(argc, argv);
  application.setApplicationName("Vulkax Physics Studio");
  application.setOrganizationName("Vulkax");
  application.setOrganizationDomain("vulkax.local");
  const bool smoke = application.arguments().contains("--smoke");
  const bool uiSmoke = application.arguments().contains("--ui-smoke");
  const bool projectSmoke = application.arguments().contains("--project-smoke");
  const bool dynamicProjectSmoke = application.arguments().contains("--dynamic-project-smoke");
  const bool unavailableErrorSmoke = application.arguments().contains("--unavailable-error-smoke");
  const bool gpuPreviewSmoke = application.arguments().contains("--gpu-preview-smoke");
  const bool gpuResizeSmoke = application.arguments().contains("--gpu-resize-smoke");
  const bool gpuReactionPreviewSmoke = application.arguments().contains("--gpu-reaction-preview-smoke");
  const bool allPresetsSmoke = application.arguments().contains("--all-presets-smoke");
  const int sequenceArgument = application.arguments().indexOf("--export-sequence");
  const int exrArgument = application.arguments().indexOf("--export-exr");

  QQuickStyle::setStyle("Basic");
  // The interactive chrome uses Metal on macOS. Vulkan remains the explicit
  // backend for the studio's compute executor, which owns its own instance and
  // device. This avoids constructing a Qt Vulkan RHI without a QVulkanInstance.
#if defined(Q_OS_MACOS)
  if (!smoke) QQuickWindow::setGraphicsApi(QSGRendererInterface::MetalRhi);
#else
  if (!smoke) QQuickWindow::setGraphicsApi(QSGRendererInterface::VulkanRhi);
#endif

  vulkax::editor::StudioController controller;
  const int presetArgument = application.arguments().indexOf("--preset");
  if (presetArgument >= 0) {
    if (presetArgument + 1 >= application.arguments().size()) {
      qCritical() << "--preset requires a preset identifier";
      return 2;
    }
    controller.selectPreset(application.arguments().at(presetArgument + 1));
  }
  if (projectSmoke) {
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QString projectPath = directory.filePath("roundtrip.vxp");
    controller.setParameter("amplitude", 0.42);
    controller.seek(1.25);
    const bool saved = controller.saveProject(projectPath);
    controller.setParameter("amplitude", 0.91);
    controller.seek(0.0);
    const bool opened = saved && controller.openProject(projectPath);
    const bool restored = std::abs(controller.timelineSeconds() - 1.25) < 1e-9 &&
        controller.expression().contains("amplitude");
    qInfo().noquote() << (opened && restored ? "Physics Studio project smoke passed" : "Physics Studio project smoke failed");
    return opened && restored ? 0 : 1;
  }
  if (dynamicProjectSmoke) {
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QString projectPath = directory.filePath("reaction-roundtrip.vxp");
    controller.selectPreset("reaction-diffusion-seed");
    controller.setParameter("feed", 0.052);
    controller.setParameter("kill", 0.071);
    controller.seek(0.5);
    const bool saved = controller.saveProject(projectPath);
    controller.setParameter("feed", 0.01);
    controller.seek(0.0);
    const bool opened = saved && controller.openProject(projectPath);
    double restoredFeed = -1.0;
    for (const auto& entry : controller.parameters()) {
      const auto map = entry.toMap();
      if (map.value("name").toString() == "feed") restoredFeed = map.value("value").toDouble();
    }
    const bool restored = controller.selectedPreset() == "reaction-diffusion-seed" &&
        std::abs(controller.timelineSeconds() - 0.5) < 1e-9 && std::abs(restoredFeed - 0.052) < 1e-9;
    qInfo().noquote() << (opened && restored ? "Physics Studio dynamic project smoke passed" :
        "Physics Studio dynamic project smoke failed");
    return opened && restored ? 0 : 1;
  }
  if (unavailableErrorSmoke) {
    controller.selectPreset("schwarzschild-lensing");
    const bool unavailable = !controller.visualErrorAvailable() && std::isnan(controller.visualError()) &&
        controller.errorMetric() == "reference unavailable";
    qInfo().noquote() << (unavailable ? "Physics Studio unavailable-error smoke passed" :
        "Physics Studio unavailable-error smoke failed");
    return unavailable ? 0 : 1;
  }
  if (gpuPreviewSmoke) {
    controller.selectPreset("wave-field");
    const bool rendered = !controller.previewImage().isNull();
    const bool gpu = controller.previewBackend().startsWith("Persistent Vulkan compute + RGBA16F frame:");
    qInfo().noquote() << (rendered && gpu ? "Physics Studio persistent GPU preview smoke passed" :
        "Physics Studio persistent GPU preview smoke unavailable: " + controller.previewBackend());
    return rendered && gpu ? 0 : 1;
  }
  if (gpuResizeSmoke) {
    controller.selectPreset("wave-field");
    controller.setPreviewExtent(160, 96, 1.0);
    const QImage first = controller.previewImage();
    controller.setPreviewExtent(512, 288, 1.0);
    const QImage second = controller.previewImage();
    const bool resized = first.size() == QSize(160, 96) && second.size() == QSize(512, 288);
    const bool gpu = controller.previewBackend().startsWith(
        "Persistent Vulkan compute + RGBA16F frame:");
    qInfo().noquote() << (resized && gpu ? "Physics Studio GPU resize smoke passed" :
        "Physics Studio GPU resize smoke failed: " + controller.previewBackend());
    return resized && gpu ? 0 : 1;
  }
  if (gpuReactionPreviewSmoke) {
    controller.selectPreset("reaction-diffusion-seed");
    controller.setParameter("feed", 0.052);
    controller.seek(0.5);
    const bool rendered = !controller.previewImage().isNull();
    const bool gpu = controller.previewBackend().startsWith("Persistent Vulkan Gray-Scott compute:");
    const bool agrees = controller.visualErrorAvailable() && controller.visualError() < 1e-10;
    qInfo().noquote() << (rendered && gpu && agrees ? "Physics Studio persistent GPU reaction preview smoke passed" :
        "Physics Studio persistent GPU reaction preview smoke failed: " + controller.previewBackend());
    return rendered && gpu && agrees ? 0 : 1;
  }
  if (allPresetsSmoke) {
    controller.setPreviewExtent(320, 180, 1.0);
    bool passed = true;
    for (const auto& entry : controller.presets()) {
      const QString preset = entry.toMap().value("id").toString();
      controller.selectPreset(preset);
      controller.seek(0.5);
      const bool compiled = controller.compileExpression();
      const QImage image = controller.previewImage();
      int minimumLuminance = 255;
      int maximumLuminance = 0;
      for (int row = 0; row < image.height(); ++row) {
        const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(row));
        for (int column = 0; column < image.width(); ++column) {
          const int luminance = qGray(pixels[column]);
          minimumLuminance = std::min(minimumLuminance, luminance);
          maximumLuminance = std::max(maximumLuminance, luminance);
        }
      }
      const bool valid = compiled && !image.isNull() && maximumLuminance > minimumLuminance;
      qInfo().noquote() << QString("%1: %2, luminance=[%3, %4], backend=%5")
                               .arg(preset, valid ? "passed" : "failed")
                               .arg(minimumLuminance)
                               .arg(maximumLuminance)
                               .arg(controller.previewBackend());
      passed = passed && valid;
    }
    qInfo().noquote() << (passed ? "Physics Studio all-presets smoke passed" :
        "Physics Studio all-presets smoke failed");
    return passed ? 0 : 1;
  }
  if (sequenceArgument >= 0) {
    if (sequenceArgument + 1 >= application.arguments().size()) {
      qCritical() << "--export-sequence requires a destination directory";
      return 2;
    }
    int frameCount = 120;
    const int framesArgument = application.arguments().indexOf("--frames");
    if (framesArgument >= 0 && framesArgument + 1 < application.arguments().size()) {
      frameCount = std::max(1, application.arguments().at(framesArgument + 1).toInt());
    }
    const bool exported = controller.exportSequence(
        application.arguments().at(sequenceArgument + 1), frameCount);
    qInfo().noquote() << (exported ? "Physics Studio sequence export passed" : "Physics Studio sequence export failed");
    return exported ? 0 : 1;
  }
  if (exrArgument >= 0) {
    if (exrArgument + 1 >= application.arguments().size()) {
      qCritical() << "--export-exr requires a destination file";
      return 2;
    }
    const bool exported = controller.exportExr(application.arguments().at(exrArgument + 1));
    qInfo().noquote() << (exported ? "Physics Studio EXR export passed" : "Physics Studio EXR export failed");
    return exported ? 0 : 1;
  }
  if (smoke) {
    const bool exported = controller.exportPng("vulkax_physics_studio_smoke.png");
    qInfo().noquote() << (exported ? "Physics Studio smoke passed" : "Physics Studio smoke failed");
    return exported ? 0 : 1;
  }

  QQmlApplicationEngine engine;
  engine.addImageProvider("vulkax-field", new FieldImageProvider(controller));
  engine.rootContext()->setContextProperty("studio", &controller);
  engine.load(QUrl("qrc:/studio/Main.qml"));
  if (engine.rootObjects().isEmpty()) return 1;

  if (uiSmoke) {
    QTimer::singleShot(100, &application, [&application] {
      qInfo() << "Physics Studio UI smoke passed";
      application.quit();
    });
  }

  QTimer timer;
  timer.setTimerType(Qt::PreciseTimer);
  QObject::connect(&timer, &QTimer::timeout, &controller, &vulkax::editor::StudioController::advance);
  if (!uiSmoke) timer.start(16);
  return application.exec();
}
