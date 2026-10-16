#pragma once

#include "DockView.h"
#include "core/KeyframeEffect.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QToolButton;

namespace fg {

class ColorButton;
class EffectLibrary;
class HidDock;
class Inspector;
class ProgressButton;
class Project;
class ShaderEditor;
class SliderRow;
class SpaceStationClient;
class Timeline;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void retranslateUi();
    void setUploadProgress(double fraction);
    void switchLanguage(const QString& language);
    void syncSettingsUi();
    enum class PaintTool { Brush, Eraser, Fill, Picker };
    enum class UploadVia { None, Hid, Service };

    QWidget* buildHeader();
    QWidget* buildToolStrip();
    QWidget* buildTransport();
    void wireProject();
    void wireDevices();
    void wireShortcuts();
    void selectTool(PaintTool t);
    void finishUpload(bool ok, const QString& message);

    void tick();
    void setPlaying(bool on);
    void setPhase(double phase);
    void renderPreview();

    KeyframeEffect* currentKeys();
    void updateToolState();
    void updateKeyUi();
    void updateBakeInfo();
    void updateTitle();
    void updateConnectionUi();

    void onStrokeBegan(bool erase);
    void onPainted(const QVector<int>& leds, const QVector<float>& weights);
    void onFill(int led);
    void onPick(int led);
    void setBrushColor(const QColor& c);

    void addKeyAtPlayhead();
    void removeSelectedKey();
    void showKeyMenu(int index, const QPoint& globalPos);

    void replaceCurrentEffect(const QString& typeId);
    void importMedia();
    void addShaderLayer();
    void openShaderEditor();
    void sendToDock();
    void showOutputSettings();

    bool openFile();
    bool saveFile(bool saveAs);
    void newFile();
    QString autosavePath() const;

    Project* m_project;
    SpaceStationClient* m_client;
    HidDock* m_hid;
    bool m_devicesEnabled = true;
    bool m_clientStarted = false;

    DockView* m_dock;
    Timeline* m_timeline;
    EffectLibrary* m_library;
    Inspector* m_inspector;
    ShaderEditor* m_shaderEditor = nullptr;

    QLabel* m_statusPill;
    QToolButton* m_newBtn;
    QToolButton* m_openBtn;
    QToolButton* m_saveBtn;
    QToolButton* m_undoBtn;
    QToolButton* m_redoBtn;
    QLabel* m_brightnessLabel;
    SliderRow* m_brightness;
    QToolButton* m_settingsBtn;
    ProgressButton* m_sendBtn;

    QWidget* m_toolStrip;
    QToolButton* m_brushBtn;
    QToolButton* m_eraserBtn;
    QToolButton* m_fillBtn;
    QToolButton* m_pickerBtn;
    QToolButton* m_mirrorBtn;
    ColorButton* m_colorBtn;
    QLabel* m_sizeLabel;
    SliderRow* m_sizeSlider;
    QLabel* m_hardLabel;
    SliderRow* m_hardSlider;
    QWidget* m_paintTools;

    QToolButton* m_playBtn;
    QLabel* m_timeLabel;
    QLabel* m_loopLabel;
    QDoubleSpinBox* m_loopSpin;
    QToolButton* m_keyAddBtn;
    QToolButton* m_keyDelBtn;
    QComboBox* m_easing;
    QLabel* m_bakeInfo;
    QTimer m_estimateTimer;
    QElapsedTimer m_uploadClock;
    double m_uploadEstimateSeconds = 0;

    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_playing = true;
    double m_phase = 0;

    PaintTool m_tool = PaintTool::Brush;
    bool m_maskMode = false;
    QColor m_brushColor{0, 200, 255};
    int m_strokeKey = -1;
    bool m_strokeErase = false;
    int m_selectedKey = -1;
    std::vector<Rgba> m_strokeBase;
    std::vector<float> m_strokeMaskBase;
    std::vector<Rgba> m_keyClipboard;

    int m_maxFrames = 200;
    bool m_dockPreview = false;
    int m_previewFrame = -1;
    std::vector<float> m_previewErrIn;
    std::vector<float> m_previewErrOut;
    int m_uploadId = -1;
    UploadVia m_uploadVia = UploadVia::None;
};

} // namespace fg
