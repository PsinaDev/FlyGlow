// Synthwave sun over a scrolling grid, drawn thick enough for 162 LEDs.
// Loops through iPhase: set "Loop crossfade" to 0.

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float horizon = 0.02;
    vec3 col;
    if (uv.y > horizon) {
        float h = uv.y - horizon;
        col = mix(vec3(0.85, 0.08, 0.45), vec3(0.12, 0.0, 0.3), clamp(h * 2.4, 0.0, 1.0));
        vec2 s = uv - vec2(0.0, 0.19);
        float sun = 1.0 - smoothstep(0.2, 0.23, length(s));
        // The cut-outs slide down two band heights per loop.
        float bands = step(0.45, fract(s.y * 7.0 + 2.0 * iPhase));
        sun *= s.y > 0.03 ? 1.0 : bands;
        vec3 sunCol = mix(vec3(1.0, 0.1, 0.35), vec3(1.0, 0.8, 0.15), clamp(s.y * 2.5 + 0.5, 0.0, 1.0));
        col = mix(col, sunCol, sun);
    } else {
        float depth = 0.25 / (horizon - uv.y + 0.03);
        vec2 grid = vec2(uv.x * depth, depth + 2.0 * iPhase);
        vec2 dist = abs(fract(grid) - 0.5);
        float line = max(smoothstep(0.38, 0.5, dist.x), smoothstep(0.36, 0.5, dist.y));
        float fade = clamp((horizon - uv.y) * 4.0, 0.0, 1.0);
        col = mix(vec3(0.08, 0.0, 0.18), vec3(1.0, 0.1, 0.6), line * fade);
        col = mix(col, vec3(0.6, 0.05, 0.5), (1.0 - fade) * 0.6);
    }
    fragColor = vec4(col, 1.0);
}
