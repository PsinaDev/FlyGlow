#include "ShaderEditor.h"

#include "Project.h"
#include "Theme.h"
#include "shader/ShaderEffect.h"
#include "shader/ShaderRenderer.h"

#include <QAction>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace fg {

namespace {

struct Preset {
    const char* file;   /**< under :/shaders; empty for the default shader */
    const char* title;
};

const Preset kPresets[] = {
    {"", QT_TRANSLATE_NOOP("fg::ShaderEditor", "Rainbow rings")},
    {"kaleidoscope.glsl", QT_TRANSLATE_NOOP("fg::ShaderEditor", "Kaleidoscope")},
    {"heartbeat.glsl", QT_TRANSLATE_NOOP("fg::ShaderEditor", "Heartbeat")},
    {"lava-lamp.glsl", QT_TRANSLATE_NOOP("fg::ShaderEditor", "Lava lamp")},
    {"warp-stars.glsl", QT_TRANSLATE_NOOP("fg::ShaderEditor", "Warp stars")},
    {"synthwave.glsl", QT_TRANSLATE_NOOP("fg::ShaderEditor", "Synthwave")},
    {"pacman.glsl", QT_TRANSLATE_NOOP("fg::ShaderEditor", "Pac-Man")},
};

QTextCharFormat textFormat(const QColor& color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(color);
    if (bold)
        f.setFontWeight(QFont::DemiBold);
    f.setFontItalic(italic);
    return f;
}

class GlslHighlighter : public QSyntaxHighlighter {
public:
    explicit GlslHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc)
    {
        auto words = [](const char* list) {
            return QRegularExpression(QStringLiteral("\\b(?:%1)\\b").arg(QString::fromLatin1(list)));
        };
        m_rules = {
            {words("if|else|for|while|do|return|break|continue|discard|switch|case|default|const|in|out|inout|"
                   "uniform|struct|precision|highp|mediump|lowp|true|false"),
             textFormat(theme::kAccent2, true)},
            {words("void|bool|int|uint|float|vec2|vec3|vec4|ivec2|ivec3|ivec4|uvec2|uvec3|uvec4|bvec2|bvec3|bvec4|"
                   "mat2|mat3|mat4|mat2x2|mat3x3|mat4x4|sampler2D"),
             textFormat(theme::kAccent)},
            {words("radians|degrees|sin|cos|tan|asin|acos|atan|sinh|cosh|tanh|pow|exp|log|exp2|log2|sqrt|"
                   "inversesqrt|abs|sign|floor|ceil|round|trunc|fract|mod|min|max|clamp|mix|step|smoothstep|"
                   "length|distance|dot|cross|normalize|reflect|refract|faceforward|transpose|inverse|"
                   "texture|texelFetch|textureLod|fwidth|dFdx|dFdy|isnan|isinf"),
             textFormat(QColor(0x7a, 0xa2, 0xf7))},
            {words("mainImage|fragColor|fragCoord|iResolution|iTime|iTimeDelta|iFrameRate|iFrame|iMouse|iDate|"
                   "iPhase|iLoop|iChannel[0-3]|iChannelResolution|iChannelTime|iSampleRate"),
             textFormat(theme::kOk)},
            {QRegularExpression(QStringLiteral(R"(\b(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?[uUfF]?\b)")),
             textFormat(QColor(0xf0, 0xb8, 0x6e))},
            {QRegularExpression(QStringLiteral(R"(^\s*#.*)")), textFormat(QColor(0xff, 0x8f, 0x6b))},
            {QRegularExpression(QStringLiteral("//.*")), textFormat(theme::kTextDim, false, true)},
        };
        m_comment = textFormat(theme::kTextDim, false, true);
    }

protected:
    void highlightBlock(const QString& text) override
    {
        for (const Rule& rule : m_rules) {
            auto it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                setFormat(int(m.capturedStart()), int(m.capturedLength()), rule.format);
            }
        }
        static const QRegularExpression open(QStringLiteral(R"(/\*)"));
        static const QRegularExpression close(QStringLiteral(R"(\*/)"));
        setCurrentBlockState(0);
        qsizetype start = previousBlockState() == 1 ? 0 : text.indexOf(open);
        while (start >= 0) {
            const QRegularExpressionMatch end = close.match(text, start);
            qsizetype length;
            if (end.hasMatch()) {
                length = end.capturedEnd() - start;
            } else {
                setCurrentBlockState(1);
                length = text.size() - start;
            }
            setFormat(int(start), int(length), m_comment);
            start = text.indexOf(open, start + length);
        }
    }

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<Rule> m_rules;
    QTextCharFormat m_comment;
};

} // namespace

class CodeEdit : public QPlainTextEdit {
public:
    explicit CodeEdit(QWidget* parent) : QPlainTextEdit(parent), m_gutter(new Gutter(this))
    {
        QFont mono;
        mono.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("Courier New")});
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSizeF(10);
        setFont(mono);
        setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')) * 4);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        new GlslHighlighter(document());

        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateMargins(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
            if (dy)
                m_gutter->scroll(0, dy);
            else
                m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
            if (rect.contains(viewport()->rect()))
                updateMargins();
        });
        updateMargins();
    }

    std::function<void()> onApply;

    void setErrorLines(const QList<int>& lines)
    {
        m_errors = lines;
        QList<QTextEdit::ExtraSelection> marks;
        QColor bg = theme::kDanger;
        bg.setAlpha(45);
        for (int line : lines) {
            const QTextBlock block = document()->findBlockByNumber(line - 1);
            if (!block.isValid())
                continue;
            QTextEdit::ExtraSelection mark;
            mark.format.setBackground(bg);
            mark.format.setProperty(QTextFormat::FullWidthSelection, true);
            mark.cursor = QTextCursor(block);
            marks.push_back(mark);
        }
        setExtraSelections(marks);
        m_gutter->update();
    }

    void goToLine(int line)
    {
        const QTextBlock block = document()->findBlockByNumber(line - 1);
        if (!block.isValid())
            return;
        setTextCursor(QTextCursor(block));
        centerCursor();
        setFocus();
    }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QPlainTextEdit::resizeEvent(e);
        const QRect r = contentsRect();
        m_gutter->setGeometry(r.left(), r.top(), gutterWidth(), r.height());
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        const bool enter = e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter;
        if (enter && (e->modifiers() & Qt::ControlModifier)) {
            if (onApply)
                onApply();
            return;
        }
        if (e->key() == Qt::Key_Tab && e->modifiers() == Qt::NoModifier) {
            insertPlainText(QStringLiteral("    "));
            return;
        }
        if (enter && e->modifiers() == Qt::NoModifier) {
            // A new line starts where the current one is indented, one step deeper after an opening brace.
            const QString line = textCursor().block().text().left(textCursor().positionInBlock());
            QString indent;
            while (indent.size() < line.size() && line[indent.size()].isSpace())
                indent += line[indent.size()];
            if (line.trimmed().endsWith(QLatin1Char('{')))
                indent += QStringLiteral("    ");
            QPlainTextEdit::keyPressEvent(e);
            insertPlainText(indent);
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }

private:
    class Gutter : public QWidget {
    public:
        explicit Gutter(CodeEdit* edit) : QWidget(edit), m_edit(edit) {}

    protected:
        void paintEvent(QPaintEvent* e) override { m_edit->paintGutter(e->rect()); }

    private:
        CodeEdit* m_edit;
    };

    int gutterWidth() const
    {
        int digits = 1;
        for (int n = std::max(1, blockCount()); n >= 10; n /= 10)
            ++digits;
        return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * std::max(3, digits);
    }

    void updateMargins() { setViewportMargins(gutterWidth(), 0, 0, 0); }

    void paintGutter(const QRect& rect)
    {
        QPainter p(m_gutter);
        p.fillRect(rect, theme::kPanel);
        p.setFont(font());
        QTextBlock block = firstVisibleBlock();
        int top = int(std::round(blockBoundingGeometry(block).translated(contentOffset()).top()));
        const int lineHeight = fontMetrics().height();
        while (block.isValid() && top <= rect.bottom()) {
            const int number = block.blockNumber() + 1;
            if (block.isVisible() && top + lineHeight >= rect.top()) {
                p.setPen(m_errors.contains(number) ? theme::kDanger : theme::kTextDim);
                p.drawText(0, top, m_gutter->width() - 8, lineHeight, Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(number));
            }
            top += int(std::round(blockBoundingRect(block).height()));
            block = block.next();
        }
    }

    Gutter* m_gutter;
    QList<int> m_errors;
};

ShaderEditor::ShaderEditor(Project* project, QWidget* parent) : QDialog(parent), m_project(project)
{
    resize(800, 700);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(8);

    m_help = new QLabel(this);
    m_help->setObjectName("Dim");
    m_help->setWordWrap(true);
    m_help->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_help);

    auto* files = new QHBoxLayout();
    files->setSpacing(6);
    m_presetsBtn = new QPushButton(this);
    auto* presets = new QMenu(m_presetsBtn);
    for (const Preset& preset : kPresets) {
        const QString file = QString::fromLatin1(preset.file);
        m_presetActions.push_back(presets->addAction(QString(), this, [this, file] {
            if (file.isEmpty()) {
                loadSource(ShaderEffect::defaultSource());
                return;
            }
            QFile f(QStringLiteral(":/shaders/") + file);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                loadSource(QString::fromUtf8(f.readAll()));
        }));
    }
    m_presetsBtn->setMenu(presets);
    m_openBtn = new QPushButton(this);
    m_saveBtn = new QPushButton(this);
    files->addWidget(m_presetsBtn);
    files->addWidget(m_openBtn);
    files->addWidget(m_saveBtn);
    files->addStretch(1);
    v->addLayout(files);

    m_code = new CodeEdit(this);
    v->addWidget(m_code, 1);

    m_issues = new QListWidget(this);
    m_issues->setMaximumHeight(120);
    m_issues->setVisible(false);
    v->addWidget(m_issues);

    auto* bottom = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    bottom->addWidget(m_status, 1);
    m_closeBtn = new QPushButton(this);
    bottom->addWidget(m_closeBtn);
    v->addLayout(bottom);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(450);
    connect(&m_debounce, &QTimer::timeout, this, &ShaderEditor::apply);
    connect(m_code, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_loading)
            m_debounce.start();
    });
    m_code->onApply = [this] {
        m_debounce.stop();
        apply();
    };
    connect(m_issues, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (const int line = item->data(Qt::UserRole).toInt(); line > 0)
            m_code->goToLine(line);
    });
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(m_openBtn, &QPushButton::clicked, this, &ShaderEditor::openFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &ShaderEditor::saveFile);
    connect(m_project, &Project::layersChanged, this, &ShaderEditor::syncFromProject);
    connect(m_project, &Project::currentChanged, this, &ShaderEditor::syncFromProject);

    retranslateUi();
    syncFromProject();
}

void ShaderEditor::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(e);
}

void ShaderEditor::retranslateUi()
{
    setWindowTitle(tr("Shader"));
    m_help->setText(tr("GLSL in the Shadertoy style: write mainImage(out vec4 fragColor, in vec2 fragCoord). "
                       "Available: iResolution, iTime, iTimeDelta, iFrame, iDate, iPhase (0..1 through the loop) "
                       "and iLoop (loop length in seconds); iMouse is always zero. Textures and buffer passes are "
                       "not supported, iChannel reads as black. Ctrl+Enter applies at once."));
    m_closeBtn->setText(tr("Close"));
    m_presetsBtn->setText(tr("Presets"));
    m_openBtn->setText(tr("Open .glsl…"));
    m_saveBtn->setText(tr("Save .glsl…"));
    for (qsizetype i = 0; i < m_presetActions.size(); ++i)
        m_presetActions[i]->setText(tr(kPresets[i].title));
    showStatus();
}

ShaderEffect* ShaderEditor::currentShader() const
{
    Layer* l = m_project->currentLayer();
    return l ? dynamic_cast<ShaderEffect*>(l->effect.get()) : nullptr;
}

void ShaderEditor::syncFromProject()
{
    m_debounce.stop();
    ShaderEffect* shader = currentShader();
    m_code->setReadOnly(!shader);
    for (QPushButton* b : {m_presetsBtn, m_openBtn, m_saveBtn})
        b->setEnabled(shader != nullptr);
    if (shader && m_code->toPlainText() != shader->source()) {
        m_loading = true;
        m_code->setPlainText(shader->source());
        m_loading = false;
    }
    apply();
}

void ShaderEditor::apply()
{
    m_issues->clear();
    QList<int> lines;
    m_compiled = false;
    ShaderEffect* shader = currentShader();
    ShaderRenderer* renderer = ShaderRenderer::instance();
    m_glMissing = !renderer || !renderer->isAvailable();
    if (shader && !m_glMissing) {
        const QString source = m_code->toPlainText();
        const ShaderCompileResult result = renderer->compile(source);
        m_compiled = result.ok;
        for (const ShaderIssue& issue : result.issues) {
            auto* item = new QListWidgetItem(issue.line > 0 ? tr("Line %1: %2").arg(issue.line).arg(issue.text)
                                                            : issue.text, m_issues);
            item->setData(Qt::UserRole, issue.line);
            if (issue.line > 0 && !lines.contains(issue.line))
                lines.push_back(issue.line);
        }
        if (result.ok && source != shader->source()) {
            m_project->checkpoint();
            shader->setSource(source);
            m_project->notifyLayerChanged(m_project->currentIndex());
        }
    }
    m_code->setErrorLines(lines);
    m_issues->setVisible(m_issues->count() > 0);
    showStatus();
}

void ShaderEditor::loadSource(const QString& source)
{
    if (!currentShader())
        return;
    QTextCursor all(m_code->document());
    all.select(QTextCursor::Document);
    all.insertText(source);
    m_debounce.stop();
    apply();
}

void ShaderEditor::openFile()
{
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open shader"), settings.value("shader/dir").toString(),
        tr("GLSL shaders (*.glsl *.frag *.fs *.txt)") + QStringLiteral(";;") + tr("All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Could not open"), QDir::toNativeSeparators(path) + "\n\n" + file.errorString());
        return;
    }
    settings.setValue("shader/dir", QFileInfo(path).absolutePath());
    loadSource(QString::fromUtf8(file.readAll()));
}

void ShaderEditor::saveFile()
{
    QSettings settings;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save shader"), settings.value("shader/dir").toString() + QStringLiteral("/shader.glsl"),
        tr("GLSL shaders (*.glsl)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(m_code->toPlainText().toUtf8()) < 0
        || !file.commit()) {
        QMessageBox::warning(this, tr("Could not save"), QDir::toNativeSeparators(path) + "\n\n" + file.errorString());
        return;
    }
    settings.setValue("shader/dir", QFileInfo(path).absolutePath());
}

void ShaderEditor::showStatus()
{
    QString text;
    QColor color = theme::kTextDim;
    if (m_glMissing) {
        text = tr("OpenGL 3.3 is not available on this PC, so shaders cannot run.");
        color = theme::kDanger;
    } else if (!currentShader()) {
        text = tr("Select a shader layer to edit its code.");
    } else if (m_compiled) {
        text = tr("Compiled: the layer plays this code.");
        color = theme::kOk;
    } else {
        text = tr("Does not compile: the layer keeps playing the last working version.");
        color = theme::kDanger;
    }
    m_status->setText(text);
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
}

} // namespace fg
