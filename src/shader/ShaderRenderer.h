#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <memory>

class QOffscreenSurface;
class QOpenGLContext;

namespace fg {

/** Per-frame values behind the Shadertoy-style uniforms. */
struct ShaderTime {
    double time = 0;    /**< iTime, seconds */
    double phase = 0;   /**< iPhase, [0, 1) through the loop */
    double loop = 4;    /**< iLoop, loop length in seconds */
};

struct ShaderIssue {
    int line = 0;   /**< 1-based line in the user's source, 0 when the driver gave none */
    QString text;
};

struct ShaderCompileResult {
    bool ok = false;
    QList<ShaderIssue> issues;
};

/**
 * Renders Shadertoy-style fragment shaders (a mainImage function) offscreen with OpenGL 3.3 core.
 * main() owns the only instance; everything runs on the GUI thread.
 */
class ShaderRenderer {
public:
    ShaderRenderer();
    ~ShaderRenderer();
    ShaderRenderer(const ShaderRenderer&) = delete;
    ShaderRenderer& operator=(const ShaderRenderer&) = delete;

    /** nullptr outside the instance's lifetime. */
    static ShaderRenderer* instance();
    /** Every render has this size, which matches the aspect of the dock outline. */
    static QSize frameSize();

    /** False when no OpenGL 3.3 context could be created. */
    bool isAvailable();
    /** Results are cached by source text. */
    ShaderCompileResult compile(const QString& source);
    /** sRGB pixels, or a null image when the source does not compile. */
    QImage render(const QString& source, const ShaderTime& t);

private:
    struct Program;
    bool makeCurrent();
    Program* program(const QString& source);
    QList<ShaderIssue> issues(const QString& log, int sourceLines) const;
    void release();

    std::unique_ptr<QOffscreenSurface> m_surface;
    std::unique_ptr<QOpenGLContext> m_context;
    bool m_failed = false;
    unsigned m_fbo = 0;
    unsigned m_target = 0;
    unsigned m_black = 0;
    unsigned m_vao = 0;
    unsigned m_vertex = 0;
    int m_userString = 1;   /**< source string and line offset the driver uses for the user's code */
    int m_lineOffset = 0;
    QHash<QString, std::shared_ptr<Program>> m_programs;
};

} // namespace fg
