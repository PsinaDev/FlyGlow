// Stars streaking out of the centre. Each star makes a whole number of trips per loop.
// Loops through iPhase: set "Loop crossfade" to 0.

const float TAU = 6.2831853;

float hash(float n)
{
    return fract(sin(n * 127.1) * 43758.5453);
}

float segment(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y - vec2(0.0, 0.11);
    vec3 col = vec3(0.0, 0.0, 0.03);
    for (int i = 0; i < 36; i++) {
        float fi = float(i);
        float angle = TAU * hash(fi + 1.0);
        float trips = 1.0 + floor(hash(fi + 2.0) * 2.0);
        float z = fract(hash(fi + 3.0) + iPhase * trips);
        vec2 dir = vec2(cos(angle), sin(angle));
        vec2 head = dir * (0.05 + 0.75 * z * z);
        vec2 tail = dir * (0.05 + 0.75 * z * z * 0.7);
        float d = segment(uv, tail, head);
        float width = 0.035 + 0.055 * z;
        vec3 tint = mix(vec3(0.35, 0.65, 1.0), vec3(1.0, 0.45, 1.0), hash(fi + 4.0));
        col += 1.6 * tint * smoothstep(width, 0.0, d) * smoothstep(0.0, 0.15, z);
    }
    fragColor = vec4(min(col, 1.0), 1.0);
}
