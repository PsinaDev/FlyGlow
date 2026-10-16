// A heart beating twice per loop, tip down like the dock.
// Loops through iPhase: set "Loop crossfade" to 0.

float dot2(vec2 v)
{
    return dot(v, v);
}

// Heart distance by Inigo Quilez (MIT).
float sdHeart(vec2 p)
{
    p.x = abs(p.x);
    if (p.y + p.x > 1.0)
        return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;
    return sqrt(min(dot2(p - vec2(0.0, 1.0)), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) * sign(p.x - p.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    // Lub-dub: a strong beat and a softer one right after it.
    float f = fract(2.0 * iPhase);
    float beat = exp(-60.0 * f * f) + 0.6 * exp(-60.0 * (f - 0.18) * (f - 0.18));

    float scale = 0.55 * (1.0 + 0.1 * beat);
    vec2 p = (uv - vec2(0.0, -0.32)) / scale;
    float d = sdHeart(p);

    // A filled heart in a triangular dock just looks like the dock, so the outline carries the shape.
    vec3 col = d < 0.0 ? vec3(0.3, 0.0, 0.04) * (0.4 + 0.6 * beat) : vec3(0.0);
    float edge = 1.0 - smoothstep(0.04, 0.11, abs(d));
    col = mix(col, mix(vec3(0.85, 0.0, 0.1), vec3(1.0, 0.3, 0.45), beat), edge);
    col += vec3(1.0, 0.05, 0.25) * exp(-20.0 * max(d, 0.0)) * 0.35 * beat * step(0.0, d);
    fragColor = vec4(col, 1.0);
}
