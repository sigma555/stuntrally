#version 120

varying vec2 uv;
varying vec4 wp;
varying vec4 p;
varying vec4 n;
varying vec4 c;

void main(void)
{
	float bridge = c.w;

	gl_FragColor = vec4(bridge * vec4(1,1,1), bridge);
}
