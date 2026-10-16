// Pac-Man chomps across the dock with a ghost on his tail, once per loop.
// Loops through iPhase: set "Loop crossfade" to 0.

const float TAU = 6.2831853;

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float y = 0.12;
    // Both are off the dock when the loop wraps round.
    float x = mix(-0.8, 1.3, iPhase);
    vec3 col = vec3(0.0, 0.0, 0.08);

    for (int i = 0; i < 9; i++) {
        float dx = -0.6 + 0.15 * float(i);
        if (dx > x + 0.05 && length(uv - vec2(dx, y)) < 0.035)
            col = vec3(1.0, 0.8, 0.6);
    }

    vec2 p = uv - vec2(x, y);
    float mouth = 0.6 * abs(sin(4.0 * TAU * iPhase));
    if (length(p) < 0.17 && abs(atan(p.y, p.x)) > mouth * 0.5)
        col = vec3(1.0, 0.85, 0.0);

    vec2 g = uv - vec2(x - 0.42, y);
    float wave = 0.025 * sin(40.0 * g.x + 3.0 * TAU * iPhase);
    bool body = (g.y > 0.0 && length(g) < 0.15) || (g.y <= 0.0 && g.y > -0.15 + wave && abs(g.x) < 0.15);
    if (body) {
        col = vec3(1.0, 0.3, 0.6);
        if (length(g - vec2(-0.05, 0.03)) < 0.04 || length(g - vec2(0.06, 0.03)) < 0.04)
            col = vec3(1.0);
    }
    fragColor = vec4(col, 1.0);
}
