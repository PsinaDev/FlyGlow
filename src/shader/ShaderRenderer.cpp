#include "ShaderRenderer.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QRegularExpression>
#include <QSurfaceFormat>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace fg {

namespace {

ShaderRenderer* g_instance = nullptr;

constexpr int kWidth = 224;
constexpr int kHeight = 190;
// Every pause in typing compiles a new program; past this many the cache starts over.
constexpr int kMaxPrograms = 24;

const char* kVertexSource = R"(#version 330 core
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kPrelude = R"(#version 330 core
uniform vec3 iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform float iFrameRate;
uniform int iFrame;
uniform vec4 iMouse;
uniform vec4 iDate;
uniform float iSampleRate;
uniform vec3 iChannelResolution[4];
uniform float iChannelTime[4];
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
uniform float iPhase;
uniform float iLoop;
out vec4 fgFragColor;
)";

const char* kEpilogue = R"(
void main()
{
    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);
    mainImage(color, gl_FragCoord.xy);
    fgFragColor = vec4(color.rgb, 1.0);
}
)";

QString shaderLog(QOpenGLExtraFunctions* gl, GLuint shader)
{
    GLint length = 0;
    gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    QByteArray log(std::max(length, 1), '\0');
    gl->glGetShaderInfoLog(shader, length, nullptr, log.data());
    return QString::fromUtf8(log.constData()).trimmed();
}

QString programLog(QOpenGLExtraFunctions* gl, GLuint program)
{
    GLint length = 0;
    gl->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    QByteArray log(std::max(length, 1), '\0');
    gl->glGetProgramInfoLog(program, length, nullptr, log.data());
    return QString::fromUtf8(log.constData()).trimmed();
}

GLuint compileStage(QOpenGLExtraFunctions* gl, GLenum type, const std::vector<QByteArray>& parts, QString* log)
{
    std::vector<const char*> text;
    std::vector<GLint> lengths;
    for (const QByteArray& p : parts) {
        text.push_back(p.constData());
        lengths.push_back(GLint(p.size()));
    }
    const GLuint shader = gl->glCreateShader(type);
    gl->glShaderSource(shader, GLsizei(text.size()), text.data(), lengths.data());
    gl->glCompileShader(shader);
    GLint ok = GL_FALSE;
    gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (log)
        *log = shaderLog(gl, shader);
    if (ok == GL_TRUE)
        return shader;
    gl->glDeleteShader(shader);
    return 0;
}

struct LogLine {
    int string = -1;   /**< -1 when the line carries no location */
    int line = 0;
    QString text;
};

// Drivers word locations differently: "1(12) :", "1:12(5):", "ERROR: 1:12:".
QList<LogLine> splitLog(const QString& log)
{
    static const QRegularExpression located(
        QStringLiteral(R"(^(?:ERROR:|WARNING:)?\s*(\d+)[:(](\d+)\)?(?:\(\d+\))?\s*:?\s*(.*)$)"));
    QList<LogLine> lines;
    for (const QString& raw : log.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString text = raw.trimmed();
        if (text.isEmpty())
            continue;
        const QRegularExpressionMatch m = located.match(text);
        if (m.hasMatch())
            lines.push_back({m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3)});
        else
            lines.push_back({-1, 0, text});
    }
    return lines;
}

} // namespace

struct ShaderRenderer::Program {
    GLuint id = 0;
    ShaderCompileResult result;
    GLint resolution = -1, time = -1, timeDelta = -1, frameRate = -1, frame = -1, mouse = -1, date = -1;
    GLint sampleRate = -1, channelResolution = -1, channelTime = -1, phase = -1, loop = -1;
    GLint channels[4] = {-1, -1, -1, -1};
};

ShaderRenderer::ShaderRenderer()
{
    g_instance = this;
}

ShaderRenderer::~ShaderRenderer()
{
    if (g_instance == this)
        g_instance = nullptr;
    release();
}

ShaderRenderer* ShaderRenderer::instance()
{
    return g_instance;
}

QSize ShaderRenderer::frameSize()
{
    return {kWidth, kHeight};
}

bool ShaderRenderer::isAvailable()
{
    if (!makeCurrent())
        return false;
    m_context->doneCurrent();
    return true;
}

bool ShaderRenderer::makeCurrent()
{
    if (m_context)
        return m_context->makeCurrent(m_surface.get());
    if (m_failed)
        return false;

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    auto context = std::make_unique<QOpenGLContext>();
    context->setFormat(format);
    auto surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(format);
    surface->create();
    if (!context->create() || !surface->isValid() || !context->makeCurrent(surface.get())
        || context->format().version() < qMakePair(3, 3)) {
        m_failed = true;
        return false;
    }
    m_context = std::move(context);
    m_surface = std::move(surface);

    QOpenGLExtraFunctions* gl = m_context->extraFunctions();
    gl->glGenTextures(1, &m_target);
    gl->glBindTexture(GL_TEXTURE_2D, m_target);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glGenFramebuffers(1, &m_fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_target, 0);

    // Shaders that sample an iChannel get black instead of whatever the unit happens to hold.
    const uchar black[4] = {0, 0, 0, 255};
    gl->glGenTextures(1, &m_black);
    gl->glBindTexture(GL_TEXTURE_2D, m_black);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    gl->glGenVertexArrays(1, &m_vao);
    m_vertex = compileStage(gl, GL_VERTEX_SHADER, {QByteArray(kVertexSource)}, nullptr);

    // Drivers disagree on how lines are numbered across source strings, so learn it from a known mistake on line 3.
    QString probeLog;
    if (const GLuint probe = compileStage(gl, GL_FRAGMENT_SHADER,
                                          {QByteArray(kPrelude), QByteArray("\n\nfloat fgProbe = fgUndefinedProbe;\n"),
                                           QByteArray(kEpilogue)},
                                          &probeLog))
        gl->glDeleteShader(probe);
    for (const LogLine& l : splitLog(probeLog))
        if (l.string >= 0 && l.text.contains(QLatin1String("fgUndefinedProbe"))) {
            m_userString = l.string;
            m_lineOffset = l.line - 3;
            break;
        }
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || !m_vertex) {
        release();
        m_failed = true;
        return false;
    }
    return true;
}

void ShaderRenderer::release()
{
    if (!m_context)
        return;
    if (m_context->makeCurrent(m_surface.get())) {
        QOpenGLExtraFunctions* gl = m_context->extraFunctions();
        for (const auto& p : std::as_const(m_programs))
            if (p->id)
                gl->glDeleteProgram(p->id);
        if (m_vertex)
            gl->glDeleteShader(m_vertex);
        if (m_vao)
            gl->glDeleteVertexArrays(1, &m_vao);
        if (m_fbo)
            gl->glDeleteFramebuffers(1, &m_fbo);
        for (GLuint* t : {&m_target, &m_black})
            if (*t)
                gl->glDeleteTextures(1, t);
        m_context->doneCurrent();
    }
    m_programs.clear();
    m_vertex = m_vao = m_fbo = m_target = m_black = 0;
    m_context.reset();
    m_surface.reset();
}

ShaderRenderer::Program* ShaderRenderer::program(const QString& source)
{
    if (const auto it = m_programs.constFind(source); it != m_programs.constEnd())
        return it->get();

    QOpenGLExtraFunctions* gl = m_context->extraFunctions();
    if (m_programs.size() >= kMaxPrograms) {
        for (const auto& p : std::as_const(m_programs))
            if (p->id)
                gl->glDeleteProgram(p->id);
        m_programs.clear();
    }

    auto p = std::make_shared<Program>();
    QString log;
    const GLuint fragment = compileStage(gl, GL_FRAGMENT_SHADER,
                                         {QByteArray(kPrelude), source.toUtf8(), QByteArray(kEpilogue)}, &log);
    if (fragment) {
        p->id = gl->glCreateProgram();
        gl->glAttachShader(p->id, m_vertex);
        gl->glAttachShader(p->id, fragment);
        gl->glLinkProgram(p->id);
        gl->glDetachShader(p->id, m_vertex);
        gl->glDetachShader(p->id, fragment);
        gl->glDeleteShader(fragment);
        GLint linked = GL_FALSE;
        gl->glGetProgramiv(p->id, GL_LINK_STATUS, &linked);
        if (linked == GL_TRUE) {
            p->result.ok = true;
        } else {
            log = programLog(gl, p->id);
            gl->glDeleteProgram(p->id);
            p->id = 0;
        }
    }
    p->result.issues = issues(log, int(source.count(QLatin1Char('\n'))) + 1);

    if (p->id) {
        auto at = [&](const char* name) { return gl->glGetUniformLocation(p->id, name); };
        p->resolution = at("iResolution");
        p->time = at("iTime");
        p->timeDelta = at("iTimeDelta");
        p->frameRate = at("iFrameRate");
        p->frame = at("iFrame");
        p->mouse = at("iMouse");
        p->date = at("iDate");
        p->sampleRate = at("iSampleRate");
        p->channelResolution = at("iChannelResolution");
        p->channelTime = at("iChannelTime");
        p->phase = at("iPhase");
        p->loop = at("iLoop");
        const char* channels[4] = {"iChannel0", "iChannel1", "iChannel2", "iChannel3"};
        for (int k = 0; k < 4; ++k)
            p->channels[k] = at(channels[k]);
    }
    m_programs.insert(source, p);
    return p.get();
}

QList<ShaderIssue> ShaderRenderer::issues(const QString& log, int sourceLines) const
{
    QList<ShaderIssue> out;
    for (const LogLine& l : splitLog(log)) {
        ShaderIssue issue;
        issue.text = l.text;
        const int line = l.line - m_lineOffset;
        if (l.string == m_userString && line >= 1 && line <= sourceLines)
            issue.line = line;
        out.push_back(issue);
    }
    return out;
}

ShaderCompileResult ShaderRenderer::compile(const QString& source)
{
    if (!makeCurrent())
        return {};
    const ShaderCompileResult result = program(source)->result;
    m_context->doneCurrent();
    return result;
}

QImage ShaderRenderer::render(const QString& source, const ShaderTime& t)
{
    if (!makeCurrent())
        return {};
    const Program* p = program(source);
    QImage image;
    if (p->result.ok) {
        QOpenGLExtraFunctions* gl = m_context->extraFunctions();
        gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        gl->glViewport(0, 0, kWidth, kHeight);
        gl->glDisable(GL_BLEND);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glUseProgram(p->id);

        const float zeros[4] = {0, 0, 0, 0};
        const float ones[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        gl->glUniform3f(p->resolution, float(kWidth), float(kHeight), 1.f);
        gl->glUniform1f(p->time, float(t.time));
        gl->glUniform1f(p->timeDelta, 0.02f);
        gl->glUniform1f(p->frameRate, 50.f);
        gl->glUniform1i(p->frame, int(std::floor(t.time * 50.0)));
        gl->glUniform4f(p->mouse, 0.f, 0.f, 0.f, 0.f);
        // A fixed date keeps renders repeatable; only the seconds follow iTime.
        gl->glUniform4f(p->date, 2026.f, 0.f, 1.f, float(t.time));
        gl->glUniform1f(p->sampleRate, 44100.f);
        gl->glUniform3fv(p->channelResolution, 4, ones);
        gl->glUniform1fv(p->channelTime, 4, zeros);
        gl->glUniform1f(p->phase, float(t.phase));
        gl->glUniform1f(p->loop, float(t.loop));
        for (int k = 0; k < 4; ++k) {
            gl->glActiveTexture(GL_TEXTURE0 + GLenum(k));
            gl->glBindTexture(GL_TEXTURE_2D, m_black);
            gl->glUniform1i(p->channels[k], k);
        }

        gl->glBindVertexArray(m_vao);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        gl->glBindVertexArray(0);

        image = QImage(kWidth, kHeight, QImage::Format_RGBA8888);
        gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
        gl->glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        // GL rows run bottom-up, like Shadertoy's fragCoord.
        image = image.flipped(Qt::Vertical);
    }
    m_context->doneCurrent();
    return image;
}

} // namespace fg
