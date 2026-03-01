// Vignette – GPU post-processing effect
// Darkens edges of the screen for a cinematic look.
uniform sampler2D texture;
uniform float     intensity;  // 0.0 – 1.0 (default ~0.4)

void main() {
    vec2 uv  = gl_TexCoord[0].xy;
    vec4 col = texture2D(texture, uv);

    // Distance from centre (normalised to [0,1])
    vec2  center = uv - 0.5;
    float dist   = length(center) * 1.414;   // sqrt(2) so corners reach 1.0
    float vig    = 1.0 - smoothstep(0.4, 1.1, dist) * intensity;

    gl_FragColor = vec4(col.rgb * vig, col.a);
}
