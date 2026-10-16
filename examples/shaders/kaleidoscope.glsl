// Kaleidoscope with the dock's own three-fold symmetry, centred on the LEDs.
// Loops through iPhase: set "Loop crossfade" to 0.

const float TAU = 6.2831853;

vec3 palette(float x)
{
    return 0.5 + 0.5 * cos(TAU * (x + vec3(0.0, 0.33, 0.67)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y - vec2(0.0, 0.11);
    float t = TAU * iPhase;

    // A third of a turn per loop looks the same as no turn at all.
    float sector = TAU / 3.0;
    float a = mod(atan(uv.y, uv.x) + t / 3.0, sector);
    a = abs(a - 0.5 * sector);
    float r = length(uv);
    vec2 p = r * vec2(cos(a), sin(a));

    float v = sin(11.0 * p.x - 2.0 * t) + sin(13.0 * p.y + t) + sin(9.0 * r - 3.0 * t);
    vec3 col = palette(0.12 * v + 0.8 * r - iPhase);
    col = pow(col, vec3(1.6));
    fragColor = vec4(col, 1.0);
}
