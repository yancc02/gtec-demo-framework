#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform sampler2D s_texture;

varying vec2 v_texcoord;

void main()
{
  gl_FragColor = texture2D(s_texture, v_texcoord);
}