// Bloom (lightweight single-pass glow approximation)
// Extracts bright areas and overlays a simple blur contribution.
uniform sampler2D texture;
uniform float     threshold;  // luminance threshold (default ~0.7)
uniform float     intensity;  // bloom strength (default ~0.4)
uniform vec2      resolution; // render target size

void main() {
    vec2  uv   = gl_TexCoord[0].xy;
    vec4  col  = texture2D(texture, uv);
    vec2  texel = 1.0 / resolution;

    // Simple 9-tap box blur on bright pixels
    vec3 bloom = vec3(0.0);
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec4  s = texture2D(texture, uv + vec2(float(x), float(y)) * texel * 3.0);
            float l = dot(s.rgb, vec3(0.299, 0.587, 0.114));
            bloom  += max(s.rgb - vec3(threshold), 0.0) * (l > threshold ? 1.0 : 0.0);
        }
    }
    bloom /= 9.0;

    gl_FragColor = vec4(col.rgb + bloom * intensity, col.a);
}
