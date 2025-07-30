#ifndef _VT_OPENGL_
#define _VT_OPENGL_

#include "lib/glad.h"
#include <stdio.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H
#include "vt_vec.h"

#define FONT_SIZE 48

typedef struct {
  unsigned int textureid;  
  vec2i size;     
  vec2i bearing;
  unsigned int advance;
} character_t;


/* Check this out:
 * -> https://github.com/tsoding/opengl-template
 */


/* for opening shader files I used open syscall
 * instead of stdio fopen */
char *slurp_file(const char * const src) {
  int fd = open(src, O_RDONLY, 0644);
  if (fd < 0) return NULL;

  size_t len = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, 0);

  char *retv = (char*) malloc(len+1);
  read(fd, retv, len);
  retv[len] = '\0';
  return retv;
}

bool compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader) {
    *shader = glCreateShader(shader_type);
    glShaderSource(*shader, 1, &source, NULL);
    glCompileShader(*shader);

    GLint compiled = 0;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLchar message[1024];
        GLsizei message_size = 0;
        glGetShaderInfoLog(*shader, sizeof(message), &message_size, message);
        fprintf(stderr, "ERROR: could not compile!\n");
        fprintf(stderr, "%.*s\n", message_size, message);
        return false;
    }

    return true;
}

bool compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader) {
    char *source = slurp_file(file_path);
    if (source == NULL) {
        fprintf(stderr, "ERROR: failed to read file `%s`: %d\n", file_path, errno);
        errno = 0;
        return false;
    }
    bool ok = compile_shader_source(source, shader_type, shader);
    if (!ok) {
        fprintf(stderr, "ERROR: failed to compile `%s` shader file\n", file_path);
    }
    free(source);
    return ok;
}

bool link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program) {
    *program = glCreateProgram();

    glAttachShader(*program, vert_shader);
    glAttachShader(*program, frag_shader);
    glLinkProgram(*program);

    GLint linked = 0;
    glGetProgramiv(*program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLsizei message_size = 0;
        GLchar message[1024];

        glGetProgramInfoLog(*program, sizeof(message), &message_size, message);
        fprintf(stderr, "Program Linking: %.*s\n", message_size, message);
    }

    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);

    return program;
}

#endif // _VT_OPENGL_
