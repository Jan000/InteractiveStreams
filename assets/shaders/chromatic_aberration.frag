// Chromatic Aberration – simulates lens colour fringing
uniform sampler2D texture;
uniform float     amount;     // pixel offset (default ~2.0)
uniform vec2      resolution; // render target size

void main() {
    vec2 uv = gl_TexCoord[0].xy;

    // Offset scales with distance from centre and the amount parameter
    vec2  center = uv - 0.5;
    float dist   = length(center);
    vec2  dir    = normalize(center + 0.0001);  // avoid division by zero
    vec2  offset = dir * dist * (amount / resolution.x);

    float r = texture2D(texture, uv + offset).r;
    float g = texture2D(texture, uv).g;
    float b = texture2D(texture, uv - offset).b;
    float a = texture2D(texture, uv).a;

    gl_FragColor = vec4(r, g, b, a);
}
