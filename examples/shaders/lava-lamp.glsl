// Lava-lamp blobs drifting along closed paths, so the loop has no seam.
// Loops through iPhase: set "Loop crossfade" to 0.

const float TAU = 6.2831853;

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float t = TAU * iPhase;
    float field = 0.0;
    for (int i = 0; i < 6; i++) {
        float fi = float(i);
        vec2 c = vec2(0.40 * sin(t * (1.0 + mod(fi, 2.0)) + fi * 1.7),
                      0.30 * cos(t * (1.0 + mod(fi, 3.0)) + fi * 2.3) + 0.06);
        float r = 0.11 + 0.035 * sin(t + fi * 1.3);
        field += r * r / dot(uv - c, uv - c);
    }
    vec3 col = vec3(0.12, 0.0, 0.22);
    col = mix(col, vec3(1.0, 0.18, 0.02), smoothstep(0.7, 1.1, field));
    col = mix(col, vec3(1.0, 0.75, 0.15), smoothstep(1.6, 2.8, field));
    fragColor = vec4(col, 1.0);
}
