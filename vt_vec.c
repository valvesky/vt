#ifndef _VT_VEC_C_
#define _VT_VEC_C_

#include "vt_vec.h"

vec2i vec2is(int a) {
  return (vec2i) {a,a};
}
vec2i vec2i_add(vec2i a, vec2i b) {
  return (vec2i) {.x = a.x + b.x,
                  .y = a.y + b.y};
}
vec2i vec2i_mul(vec2i a, vec2i b) {
  return (vec2i) {.x = a.x * b.x,
                  .y = a.y * b.y};
}

vec2f vec2fs(float a) {
  return (vec2f) {a, a};
}

vec2f vec2f_mul(vec2f a, vec2f b) {
  return (vec2f) { a.x*b.x, a.y*b.y };
}

vec2f vec2f_mul3(vec2f a, vec2f b, vec2f c) {
  return vec2f_mul(c, vec2f_mul(a, b));
}

vec2f vec2f_add(vec2f a, vec2f b) {
  return (vec2f) { a.x+b.x, a.y+b.y };
}

vec4f vec4f_mul(vec4f a, vec4f b) {
  return (vec4f) { a.i*b.i, a.j*b.j, a.k*b.k, a.l*b.l };
}

vec4f vec4f_add(vec4f a, vec4f b) {
  return (vec4f) { a.i+b.i, a.j+b.j, a.k+b.k, a.l+b.l };
}

mat4f mat4f_identity() {
  return (mat4f){
    .col0 = {1,0,0,0},
    .col1 = {0,1,0,0},
    .col2 = {0,0,1,0},
    .col3 = {0,0,0,1}
  };
}

mat4f mat4f_ortho(float left, float right, float bottom, float top, float near, float far) {
  float rl = right - left;
  float tb = top - bottom;
  float fn = far - near;

  return (mat4f){
    .col0 = { 2.0f / rl, 0.0f,       0.0f,        0.0f },
    .col1 = { 0.0f,      2.0f / tb,  0.0f,        0.0f },
    .col2 = { 0.0f,      0.0f,      -2.0f / fn,   0.0f },
    .col3 = {
      -(right + left) / rl,
      -(top + bottom) / tb,
      -(far + near) / fn,
      1.0f
    }
  };
}

vec4f mat4f_mul_vec4f(mat4f m, vec4f v) {
  return (vec4f){
    m.col0.i * v.i + m.col1.i * v.j + m.col2.i * v.k + m.col3.i * v.l,
    m.col0.j * v.i + m.col1.j * v.j + m.col2.j * v.k + m.col3.j * v.l,
    m.col0.k * v.i + m.col1.k * v.j + m.col2.k * v.k + m.col3.k * v.l,
    m.col0.l * v.i + m.col1.l * v.j + m.col2.l * v.k + m.col3.l * v.l
  };
}

#endif
