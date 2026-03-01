// CRT – retro cathode-ray-tube effect
// Combines scanlines, slight barrel distortion, and RGB cell structure.
uniform sampler2D texture;
uniform float     scanlineIntensity; // 0.0 – 1.0 (default ~0.15)
uniform float     curvature;         // barrel distortion (default ~0.03)
uniform vec2      resolution;        // render target size

vec2 barrelDistort(vec2 uv, float k) {
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    return uv + c * r2 * k;
}

void main() {
    vec2 uv = barrelDistort(gl_TexCoord[0].xy, curvature);

    // Black outside the [0,1] range after distortion
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 col = texture2D(texture, uv);

    // Scanlines (horizontal dark bands)
    float scanline = sin(uv.y * resolution.y * 3.14159) * 0.5 + 0.5;
    scanline = 1.0 - scanlineIntensity * (1.0 - scanline);

    // RGB sub-pixel structure (vertical stripes)
    float px = mod(gl_FragCoord.x, 3.0);
    vec3 mask = vec3(1.0);
    if (px < 1.0)       mask = vec3(1.0, 0.7, 0.7);
    else if (px < 2.0)  mask = vec3(0.7, 1.0, 0.7);
    else                 mask = vec3(0.7, 0.7, 1.0);

    gl_FragColor = vec4(col.rgb * scanline * mask, col.a);
}
