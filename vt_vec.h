#ifndef _VT_VEC_H_
#define _VT_VEC_H_

typedef struct {int x, y; } vec2i;
// typedef struct {int i, j, k, l; } vec4i;

typedef struct {float x, y; } vec2f;
typedef struct {float i, j, k, l; } vec4f;
typedef struct { vec4f col0, col1, col2, col3; } mat4f;

vec2i vec2is(int a);
vec2i vec2i_add(vec2i a, vec2i b);
vec2i vec2i_mul(vec2i a, vec2i b);

vec2f vec2fs(float a);
vec2f vec2f_mul(vec2f a, vec2f b);
vec2f vec2f_mul3(vec2f a, vec2f b, vec2f c);
vec2f vec2f_add(vec2f a, vec2f b);

vec4f vec4f_mul(vec4f a, vec4f b);
vec4f vec4f_add(vec4f a, vec4f b);

mat4f mat4f_identity();
mat4f mat4f_ortho(float left, float right, float bottom, float top, float near, float far);
vec4f mat4f_mul_vec4f(mat4f m, vec4f v);

#endif

