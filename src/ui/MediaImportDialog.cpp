#include "MediaImportDialog.h"

#include "Controls.h"
#include "DockView.h"
#include "Theme.h"
#include "core/LedLayout.h"
#include "core/MediaEffect.h"
#include "core/Scene.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>

namespace fg {

namespace {

constexpr double kFirstVideoWindowSeconds = 10.0;
constexpr double kMinZoom = 0.25;
constexpr double kMaxZoom = 4.0;
constexpr double kMinClipSeconds = 0.05;

// The theme's QSS pins text colours and ToggleSwitch never greys out, so disabled controls are faded by hand.
void setControlEnabled(QWidget* w, bool enabled)
{
    w->setEnabled(enabled);
    if (enabled) {
        w->setGraphicsEffect(nullptr);
        return;
    }
    auto* fade = new QGraphicsOpacityEffect(w);
    fade->setOpacity(0.35);
    w->setGraphicsEffect(fade);
}

QString fileFilter()
{
    const auto globs = [](const QStringList& suffixes) {
        QStringList out;
        for (const QString& s : suffixes)
            out << QStringLiteral("*.") + s;
        return out.join(' ');
    };
    const QStringList animated = {"gif", "webp", "png", "apng"};
    QStringList images = media::imageSuffixes();
    for (const QString& s : animated)
        if (!images.contains(s))
            images << s;
    const QStringList videos = media::videoSuffixes();
    return QStringList{MediaImportDialog::tr("Images and videos (%1)").arg(globs(images + videos)),
                       MediaImportDialog::tr("Animated images (%1)").arg(globs(animated)),
                       MediaImportDialog::tr("Videos (%1)").arg(globs(videos)),
                       MediaImportDialog::tr("All files (*)")}
        .join(QStringLiteral(";;"));
}

} // namespace

namespace detail {

class MediaSourcePreview : public QWidget {
public:
    explicit MediaSourcePreview(QWidget* parent) : QWidget(parent)
    {
        setCursor(Qt::OpenHandCursor);
        setMinimumSize(360, 300);
    }

    std::function<void(const QPointF& viewDelta)> panned;
    std::function<void(double factor)> zoomed;
    std::function<void()> resetRequested;

    void setFrame(const QImage& frame)
    {
        m_frame = frame;
        update();
    }
    void setFraming(const media::Framing& framing)
    {
        m_framing = framing;
        update();
    }
    void setMessage(const QString& text)
    {
        m_message = text;
        update();
    }
    bool hasFrame() const { return !m_frame.isNull(); }

    QSize sizeHint() const override { return {560, 460}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    QTransform viewToWidget(double* scale = nullptr) const;

    QImage m_frame;
    media::Framing m_framing;
    QString m_message;
    bool m_dragging = false;
    QPointF m_last;
};

QTransform MediaSourcePreview::viewToWidget(double* scale) const
{
    const LedLayout& layout = LedLayout::cd2();
    // Fit to the outline at zoom 1 without pan, so the view holds still while the user drags or zooms.
    media::Framing rest = m_framing;
    rest.zoom = 1.0;
    rest.panX = rest.panY = 0;
    const QSize v = media::viewSize(m_frame.size(), m_framing.rotation);
    const QRectF content = QRectF(QPointF(0, 0), QSizeF(v))
                               .united(media::layoutToView(layout, m_frame.size(), rest).mapRect(layout.outline().boundingRect()));
    const QRectF area = QRectF(rect()).adjusted(20, 20, -20, -40);
    const double s = std::min(area.width() / content.width(), area.height() / content.height());
    QTransform t;
    t.translate(area.center().x(), area.center().y());
    t.scale(s, s);
    t.translate(-content.center().x(), -content.center().y());
    if (scale)
        *scale = s;
    return t;
}

void MediaSourcePreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), theme::kWindow);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_frame.isNull()) {
        p.setPen(theme::kTextDim);
        p.drawText(QRectF(rect()).adjusted(30, 30, -30, -30), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    const LedLayout& layout = LedLayout::cd2();
    const QTransform toWidget = viewToWidget();
    const QTransform layoutToWidget = media::layoutToView(layout, m_frame.size(), m_framing) * toWidget;
    const QPainterPath outline = layoutToWidget.map(layout.outline());

    p.fillPath(outline, Qt::black);
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(media::sourceToView(m_frame.size(), m_framing) * toWidget);
    p.drawImage(QPointF(0, 0), m_frame);
    p.restore();

    QPainterPath veil;
    veil.setFillRule(Qt::OddEvenFill);
    veil.addRect(rect());
    veil.addPath(outline);
    QColor shade = theme::kWindow;
    shade.setAlpha(175);
    p.fillPath(veil, shade);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 80));
    for (const Led& led : layout.leds())
        p.drawEllipse(layoutToWidget.map(led.pos), 1.3, 1.3);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme::kAccent, 1.6));
    p.drawPath(outline);

    p.setPen(theme::kTextDim);
    QFont f = font();
    f.setPointSizeF(9);
    p.setFont(f);
    p.drawText(QRectF(0, height() - 34, width(), 26), Qt::AlignCenter,
               MediaImportDialog::tr("Drag to move · Wheel to zoom · Double-click to reset"));
}

void MediaSourcePreview::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || m_frame.isNull())
        return;
    m_dragging = true;
    m_last = e->position();
    setCursor(Qt::ClosedHandCursor);
}

void MediaSourcePreview::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    double s = 1;
    viewToWidget(&s);
    const QPointF delta = (e->position() - m_last) / std::max(1e-6, s);
    m_last = e->position();
    if (panned)
        panned(delta);
}

void MediaSourcePreview::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
}

void MediaSourcePreview::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && resetRequested)
        resetRequested();
}

void MediaSourcePreview::wheelEvent(QWheelEvent* e)
{
    if (m_frame.isNull() || !zoomed)
        return;
    zoomed(std::pow(1.15, e->angleDelta().y() / 120.0));
    e->accept();
}

} // namespace detail

struct MediaImportDialog::DecodeJob {
    std::atomic<bool> cancel{false};
    std::atomic<int> permille{-1};
};

std::unique_ptr<Layer> MediaImportDialog::run(QWidget* parent, double* suggestedLoopSeconds)
{
    if (suggestedLoopSeconds)
        *suggestedLoopSeconds = 0;
    QSettings settings;
    const QString dir = settings
                            .value("media/lastDir",
                                   QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                            .toString();
    const QString path = QFileDialog::getOpenFileName(parent, tr("Import media"), dir, fileFilter());
    if (path.isEmpty())
        return nullptr;
    settings.setValue("media/lastDir", QFileInfo(path).absolutePath());

    MediaImportDialog dialog(path, parent);
    if (dialog.exec() != QDialog::Accepted)
        return nullptr;
    if (suggestedLoopSeconds && dialog.m_matchLoop->isChecked() && dialog.m_effect->isAnimated())
        *suggestedLoopSeconds = dialog.m_effect->clipSeconds();
    return dialog.takeLayer();
}

MediaImportDialog::MediaImportDialog(const QString& path, QWidget* parent)
    : QDialog(parent), m_path(path), m_sampler(LedLayout::cd2()), m_effect(std::make_unique<MediaEffect>())
{
    setWindowTitle(tr("Import %1").arg(QFileInfo(path).fileName()));
    setModal(true);
    resize(1180, 780);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* stage = new QWidget(this);
    stage->setObjectName("Stage");
    stage->setAttribute(Qt::WA_StyledBackground);
    auto* stageLayout = new QHBoxLayout(stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(1);
    m_source = new detail::MediaSourcePreview(stage);
    m_source->setMessage(tr("Reading %1…").arg(QFileInfo(path).fileName()));
    m_dock = new DockView(stage);
    m_dock->setTool(DockView::Tool::None);
    m_dock->setMinimumSize(360, 300);
    stageLayout->addWidget(m_source, 1);
    stageLayout->addWidget(m_dock, 1);
    root->addWidget(stage, 1);

    m_progressRow = new QWidget(this);
    m_progressRow->setObjectName("Panel");
    m_progressRow->setAttribute(Qt::WA_StyledBackground);
    {
        auto* row = new QHBoxLayout(m_progressRow);
        row->setContentsMargins(16, 8, 16, 8);
        row->setSpacing(12);
        m_progressLabel = new QLabel(m_progressRow);
        m_progressLabel->setObjectName("Dim");
        m_progressLabel->setMinimumWidth(150);
        m_progress = new QProgressBar(m_progressRow);
        m_progress->setRange(0, 1000);
        m_progress->setTextVisible(false);
        m_progress->setFixedHeight(6);
        auto* stop = new QPushButton(tr("Stop"), m_progressRow);
        stop->setToolTip(tr("Stop decoding"));
        row->addWidget(m_progressLabel);
        row->addWidget(m_progress, 1);
        row->addWidget(stop);
        connect(stop, &QPushButton::clicked, this, [this] {
            m_restartDecode = false;
            if (m_job)
                m_job->cancel = true;
        });
    }
    m_progressRow->hide();
    root->addWidget(m_progressRow);

    root->addWidget(buildControls());

    auto* bar = new QWidget(this);
    bar->setObjectName("Panel");
    bar->setAttribute(Qt::WA_StyledBackground);
    {
        auto* row = new QHBoxLayout(bar);
        row->setContentsMargins(16, 6, 16, 14);
        row->setSpacing(10);
        m_info = new QLabel(bar);
        m_info->setObjectName("Dim");
        auto* cancel = new QPushButton(tr("Cancel"), bar);
        m_import = new QPushButton(tr("Import"), bar);
        m_import->setObjectName("Primary");
        m_import->setDefault(true);
        row->addWidget(m_info, 1);
        row->addWidget(cancel);
        row->addWidget(m_import);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_import, &QPushButton::clicked, this, [this] {
            if (m_resampleTimer->isActive())
                resample();
            accept();
        });
    }
    root->addWidget(bar);

    m_decodeTimer = new QTimer(this);
    m_decodeTimer->setSingleShot(true);
    m_decodeTimer->setInterval(450);
    connect(m_decodeTimer, &QTimer::timeout, this, &MediaImportDialog::startDecode);

    m_resampleTimer = new QTimer(this);
    m_resampleTimer->setSingleShot(true);
    m_resampleTimer->setInterval(30);
    connect(m_resampleTimer, &QTimer::timeout, this, &MediaImportDialog::resample);

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(33);
    connect(m_playTimer, &QTimer::timeout, this, &MediaImportDialog::tick);

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(80);
    connect(m_progressTimer, &QTimer::timeout, this, [this] {
        const int permille = m_job ? m_job->permille.load() : -1;
        if (permille < 0) {
            m_progress->setRange(0, 0);
            m_progressLabel->setText(tr("Decoding…"));
        } else {
            m_progress->setRange(0, 1000);
            m_progress->setValue(permille);
            m_progressLabel->setText(tr("Decoding… %1%").arg(permille / 10));
        }
    });

    connect(&m_watcher, &QFutureWatcher<media::DecodedClip>::finished, this, &MediaImportDialog::onDecodeFinished);

    m_source->panned = [this](const QPointF& delta) {
        if (m_clip.frames.empty())
            return;
        const QSize v = media::viewSize(m_clip.frames.front().size(), m_framing.rotation);
        m_framing.panX = std::clamp(m_framing.panX + delta.x() / std::max(1, v.width()), -1.0, 1.0);
        m_framing.panY = std::clamp(m_framing.panY + delta.y() / std::max(1, v.height()), -1.0, 1.0);
        m_source->setFraming(m_framing);
        scheduleResample();
    };
    m_source->zoomed = [this](double factor) { setZoom(m_framing.zoom * factor); };
    m_source->resetRequested = [this] {
        m_framing.panX = m_framing.panY = 0;
        setZoom(1.0);
    };

    updateButtons();
    startDecode();
}

MediaImportDialog::~MediaImportDialog()
{
    if (m_job)
        m_job->cancel = true;
    m_watcher.waitForFinished();
}

QWidget* MediaImportDialog::buildControls()
{
    auto* panel = new QWidget(this);
    panel->setObjectName("Panel");
    panel->setAttribute(Qt::WA_StyledBackground);
    auto* columns = new QHBoxLayout(panel);
    columns->setContentsMargins(16, 6, 16, 6);
    columns->setSpacing(32);

    const auto column = [&](const QString& title) {
        auto* box = new QWidget(panel);
        auto* grid = new QGridLayout(box);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(1, 1);
        grid->setColumnMinimumWidth(1, 200);
        auto* heading = new QLabel(title.toUpper(), box);
        heading->setObjectName("SectionTitle");
        grid->addWidget(heading, 0, 0, 1, 2);
        columns->addWidget(box, 1);
        return grid;
    };
    const auto addRow = [](QGridLayout* grid, const QString& label, QWidget* editor) {
        const int row = grid->rowCount();
        auto* text = new QLabel(label, editor->parentWidget());
        grid->addWidget(text, row, 0);
        grid->addWidget(editor, row, 1);
        return text;
    };
    const auto addToggle = [](QGridLayout* grid, const QString& label, ToggleSwitch* toggle) {
        auto* cell = new QWidget(toggle->parentWidget());
        auto* lay = new QHBoxLayout(cell);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(10);
        toggle->setParent(cell);
        lay->addWidget(toggle);
        auto* text = new QLabel(label, cell);
        lay->addWidget(text, 1);
        grid->addWidget(cell, grid->rowCount(), 0, 1, 2);
        return cell;
    };

    QGridLayout* framing = column(tr("Framing"));
    QWidget* framingBox = framing->parentWidget();
    m_fit = new QComboBox(framingBox);
    m_fit->addItems({tr("Cover"), tr("Contain"), tr("Stretch")});
    m_fit->setToolTip(tr("Cover fills the dock and crops the image; Contain shows the whole image; "
                         "Stretch fills the dock exactly, ignoring the aspect ratio"));
    addRow(framing, tr("Fit"), m_fit);
    m_zoom = new SliderRow(framingBox);
    m_zoom->setRange(kMinZoom * 100, kMaxZoom * 100, 1, QStringLiteral("%"));
    m_zoom->setValue(100);
    addRow(framing, tr("Zoom"), m_zoom);
    m_rotation = new QComboBox(framingBox);
    m_rotation->addItems({QStringLiteral("0°"), QStringLiteral("90°"), QStringLiteral("180°"), QStringLiteral("270°")});
    m_rotation->setToolTip(tr("Turn the image clockwise"));
    addRow(framing, tr("Rotation"), m_rotation);
    m_flip = new ToggleSwitch(framingBox);
    {
        QWidget* flipRow = addToggle(framing, tr("Mirror"), m_flip);
        auto* reset = new QPushButton(tr("Reset position"), flipRow);
        reset->setObjectName("Flat");
        reset->setToolTip(tr("Centre the dock on the image at 100% zoom"));
        flipRow->layout()->addWidget(reset);
        connect(reset, &QPushButton::clicked, this, [this] { m_source->resetRequested(); });
    }
    framing->setRowStretch(framing->rowCount(), 1);

    QGridLayout* clip = column(tr("Clip"));
    QWidget* clipBox = clip->parentWidget();
    m_trimStart = new SliderRow(clipBox);
    m_trimStart->setRange(0, 1, 0.01, tr("s"));
    m_trimEnd = new SliderRow(clipBox);
    m_trimEnd->setRange(0, 1, 0.01, tr("s"));
    m_trimEnd->setValue(1);
    m_maxFrames = new SliderRow(clipBox);
    m_maxFrames->setRange(2, MediaEffect::kMaxFrames, 1);
    m_maxFrames->setValue(200);
    m_maxFrames->setToolTip(tr("Longer or faster clips are evenly thinned out to this many frames"));
    m_clipControls << m_trimStart << addRow(clip, tr("Start"), m_trimStart);
    m_clipControls << m_trimEnd << addRow(clip, tr("End"), m_trimEnd);
    m_clipControls << m_maxFrames << addRow(clip, tr("Max frames"), m_maxFrames);
    m_sourceInfo = new QLabel(clipBox);
    m_sourceInfo->setObjectName("Dim");
    m_sourceInfo->setWordWrap(true);
    m_sourceInfo->setMinimumHeight(2 * m_sourceInfo->fontMetrics().lineSpacing());
    m_sourceInfo->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    clip->addWidget(m_sourceInfo, clip->rowCount(), 0, 1, 2);
    clip->setRowStretch(clip->rowCount(), 1);

    QGridLayout* look = column(tr("Look"));
    QWidget* lookBox = look->parentWidget();
    m_brightness = new SliderRow(lookBox);
    m_brightness->setRange(0, 200, 1, QStringLiteral("%"));
    m_brightness->setValue(100);
    addRow(look, tr("Brightness"), m_brightness);
    m_saturation = new SliderRow(lookBox);
    m_saturation->setRange(0, 200, 1, QStringLiteral("%"));
    m_saturation->setValue(100);
    addRow(look, tr("Saturation"), m_saturation);
    m_smooth = new ToggleSwitch(lookBox);
    m_smooth->setChecked(true);
    m_smooth->setToolTip(tr("Blend neighbouring frames so low frame rates play smoothly"));
    m_clipControls << addToggle(look, tr("Smooth frames"), m_smooth);
    m_matchLoop = new ToggleSwitch(lookBox);
    m_matchLoop->setChecked(true);
    m_matchLoop->setToolTip(tr("Set the scene's loop length to the length of the clip"));
    m_clipControls << addToggle(look, tr("Match loop length to clip"), m_matchLoop);
    look->setRowStretch(look->rowCount(), 1);
    for (QWidget* w : m_clipControls)
        setControlEnabled(w, false);

    connect(m_fit, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_framing.fit = media::FitMode(index);
        m_source->setFraming(m_framing);
        scheduleResample();
    });
    connect(m_zoom, &SliderRow::valueChanged, this, [this](double v) { setZoom(v / 100.0); });
    connect(m_rotation, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_framing.rotation = index * 90;
        m_source->setFraming(m_framing);
        scheduleResample();
    });
    connect(m_flip, &ToggleSwitch::toggled, this, [this](bool on) {
        m_framing.flip = on;
        m_source->setFraming(m_framing);
        scheduleResample();
    });
    connect(m_trimStart, &SliderRow::valueChanged, this, [this](double v) {
        if (m_trimEnd->value() < v + kMinClipSeconds)
            m_trimEnd->setValue(std::min(m_sourceDuration, v + kMinClipSeconds));
        scheduleDecode();
    });
    connect(m_trimEnd, &SliderRow::valueChanged, this, [this](double v) {
        if (m_trimStart->value() > v - kMinClipSeconds)
            m_trimStart->setValue(std::max(0.0, v - kMinClipSeconds));
        scheduleDecode();
    });
    connect(m_maxFrames, &SliderRow::valueChanged, this, [this] { scheduleDecode(); });
    connect(m_brightness, &SliderRow::valueChanged, this, [this](double v) {
        m_adjust.brightness = v / 100.0;
        scheduleResample();
    });
    connect(m_saturation, &SliderRow::valueChanged, this, [this](double v) {
        m_adjust.saturation = v / 100.0;
        scheduleResample();
    });
    connect(m_smooth, &ToggleSwitch::toggled, this, [this](bool on) {
        m_effect->setValue("smooth", on);
        if (!m_playTimer->isActive())
            tick();
    });

    return panel;
}

void MediaImportDialog::setZoom(double zoom)
{
    m_framing.zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    m_zoom->setValue(m_framing.zoom * 100.0);
    m_source->setFraming(m_framing);
    scheduleResample();
}

void MediaImportDialog::scheduleDecode()
{
    m_decodeTimer->start();
    updateButtons();
}

void MediaImportDialog::scheduleResample()
{
    if (!m_resampleTimer->isActive())
        m_resampleTimer->start();
}

void MediaImportDialog::startDecode()
{
    if (m_watcher.isRunning()) {
        if (m_job)
            m_job->cancel = true;
        m_restartDecode = true;
        return;
    }

    media::DecodeOptions options;
    options.maxFrames = int(std::lround(m_maxFrames->value()));
    if (m_rangesReady) {
        options.trimStartSeconds = m_trimStart->value();
        options.trimEndSeconds = m_trimEnd->value();
    } else if (media::videoSuffixes().contains(QFileInfo(m_path).suffix().toLower())) {
        options.trimEndSeconds = kFirstVideoWindowSeconds;
    }

    m_job = std::make_shared<DecodeJob>();
    const std::shared_ptr<DecodeJob> job = m_job;
    const QString path = m_path;
    m_watcher.setFuture(QtConcurrent::run([job, path, options] {
        return media::decodeFile(path, options, [job](double progress) {
            job->permille = progress < 0 ? -1 : int(progress * 1000);
            return !job->cancel.load();
        });
    }));

    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progressLabel->setText(tr("Decoding…"));
    m_progressRow->show();
    m_progressTimer->start();
    updateButtons();
}

void MediaImportDialog::onDecodeFinished()
{
    media::DecodedClip clip = m_watcher.result();
    if (m_restartDecode) {
        m_restartDecode = false;
        startDecode();
        return;
    }
    m_progressTimer->stop();
    m_progressRow->hide();

    if (!m_rangesReady && (clip.ok() || clip.sourceDuration > 0))
        setupTrimRanges(clip.sourceDuration);

    if (!clip.ok()) {
        const QString message = clip.cancelled ? tr("Decoding was stopped.") : clip.error;
        if (m_clip.frames.empty()) {
            m_source->setMessage(message + (m_rangesReady && clip.cancelled
                                                ? QStringLiteral("\n") + tr("Choose a shorter range to try again.")
                                                : QString()));
        } else {
            m_info->setText(message);
        }
        updateButtons();
        return;
    }

    m_clip = std::move(clip);
    m_frameStarts.resize(m_clip.durations.size());
    double t = 0;
    for (size_t k = 0; k < m_clip.durations.size(); ++k) {
        m_frameStarts[k] = t;
        t += m_clip.durations[k];
    }
    m_shownFrame = -1;
    resample();
    m_clock.restart();
    if (m_effect->isAnimated())
        m_playTimer->start();
    else
        m_playTimer->stop();
    updateInfo();
    updateButtons();
}

void MediaImportDialog::setupTrimRanges(double duration)
{
    m_rangesReady = true;
    m_sourceDuration = duration;
    const bool animated = duration > 0;
    for (QWidget* w : m_clipControls) {
        setControlEnabled(w, true);
        w->setVisible(animated);
    }
    if (!animated) {
        m_matchLoop->setChecked(false);
        return;
    }
    const bool video = media::videoSuffixes().contains(QFileInfo(m_path).suffix().toLower());
    m_trimStart->setRange(0, duration, 0.01, tr("s"));
    m_trimEnd->setRange(0, duration, 0.01, tr("s"));
    m_trimStart->setValue(0);
    m_trimEnd->setValue(video ? std::min(duration, kFirstVideoWindowSeconds) : duration);
}

void MediaImportDialog::resample()
{
    m_resampleTimer->stop();
    if (m_clip.frames.empty())
        return;
    m_effect->setClip(m_sampler.sampleAll(m_clip.frames, m_framing, m_adjust), m_clip.durations);
    m_effect->setValue("smooth", m_smooth->isChecked());
    tick();
}

void MediaImportDialog::tick()
{
    if (m_effect->frameCount() == 0)
        return;
    const double length = m_effect->clipSeconds();
    const double t = m_effect->isAnimated() && length > 0 ? std::fmod(m_clock.elapsed() / 1000.0, length) : 0.0;

    RenderContext ctx;
    ctx.phase = length > 0 ? t / length : 0.0;
    ctx.loopSeconds = length;
    ctx.layout = &LedLayout::cd2();
    Frame frame(size_t(LedLayout::cd2().count()));
    m_effect->render(ctx, frame);
    std::vector<Rgb> colors(frame.size());
    for (size_t i = 0; i < frame.size(); ++i)
        colors[i] = {frame[i].r * frame[i].a, frame[i].g * frame[i].a, frame[i].b * frame[i].a};
    m_dock->setColors(colors);
    showFrameAt(t);
}

void MediaImportDialog::showFrameAt(double clipSeconds)
{
    if (m_clip.frames.empty())
        return;
    const auto next = std::upper_bound(m_frameStarts.begin(), m_frameStarts.end(), clipSeconds);
    const int index = next == m_frameStarts.begin()
                          ? 0
                          : std::min(int(m_clip.frames.size()) - 1, int(next - m_frameStarts.begin()) - 1);
    if (index == m_shownFrame && m_source->hasFrame())
        return;
    m_shownFrame = index;
    m_source->setFraming(m_framing);
    m_source->setFrame(m_clip.frames[size_t(index)]);
}

void MediaImportDialog::updateInfo()
{
    const int frames = m_effect->frameCount();
    if (frames == 0) {
        m_info->clear();
        m_sourceInfo->clear();
        return;
    }
    if (m_effect->isAnimated()) {
        const double length = m_effect->clipSeconds();
        m_info->setText(tr("%1 frames · %2 s · %3 fps")
                            .arg(frames)
                            .arg(length, 0, 'f', 2)
                            .arg(frames / std::max(1e-6, length), 0, 'f', 1));
    } else {
        m_info->setText(tr("Still image"));
    }

    QStringList parts;
    parts << QStringLiteral("%1×%2").arg(m_clip.sourceSize.width()).arg(m_clip.sourceSize.height());
    if (m_clip.sourceDuration > 0)
        parts << tr("%1 s").arg(m_clip.sourceDuration, 0, 'f', 2);
    if (m_clip.sourceFps > 0)
        parts << tr("%1 fps").arg(m_clip.sourceFps, 0, 'f', 2);
    QString text = tr("Source: %1").arg(parts.join(QStringLiteral(" · ")));
    if (m_clip.resampled)
        text += QStringLiteral("\n") + tr("Frames are evenly thinned out to fit the limit.");
    m_sourceInfo->setText(text);
}

void MediaImportDialog::updateButtons()
{
    m_import->setEnabled(!m_clip.frames.empty() && !m_watcher.isRunning() && !m_decodeTimer->isActive());
}

std::unique_ptr<Layer> MediaImportDialog::takeLayer() const
{
    auto layer = std::make_unique<Layer>();
    layer->name = QFileInfo(m_path).completeBaseName();
    layer->effect = m_effect->clone();
    return layer;
}

} // namespace fg
